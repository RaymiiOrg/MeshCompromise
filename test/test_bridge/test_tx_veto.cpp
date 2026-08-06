#include <gtest/gtest.h>

#include "fake_sx_driver.h"
#include "meshcompromise/radio_arbiter.h"
#include "meshcompromise/slice_scheduler.h"

using namespace meshcompromise;

namespace
{

class HostTransmitter
{
  public:
    explicit HostTransmitter(const RadioArbiter &arbiter) : arbiter_(arbiter) {}

    bool vetoed() const { return arbiter_.leaseHeld(); }

    void offer()
    {
        offered_++;
        if (vetoed()) {
            deferred_++;
            return;
        }
        transmitted_++;
    }

    uint32_t offered() const { return offered_; }
    uint32_t deferred() const { return deferred_; }
    uint32_t transmitted() const { return transmitted_; }

  private:
    const RadioArbiter &arbiter_;
    uint32_t offered_ = 0;
    uint32_t deferred_ = 0;
    uint32_t transmitted_ = 0;
};

class VetoTest : public ::testing::Test
{
  protected:
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter{driver, host};
    HostTransmitter meshtastic{arbiter};

    LoraProfile meshcore = meshcoreDefaultProfile();
    LoraProfile meshtasticProfile = meshtasticNarrowSlowProfile();

    void SetUp() override { driver.active = meshtasticProfile; }
};

} // namespace

TEST_F(VetoTest, NoVetoBeforeAnySlice)
{
    EXPECT_FALSE(meshtastic.vetoed());
    meshtastic.offer();
    EXPECT_EQ(meshtastic.transmitted(), 1u);
}

TEST_F(VetoTest, VetoIsActiveForTheWholeSlice)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    EXPECT_TRUE(meshtastic.vetoed());

    meshtastic.offer();
    EXPECT_EQ(meshtastic.transmitted(), 0u);
    EXPECT_EQ(meshtastic.deferred(), 1u);
}

TEST_F(VetoTest, VetoLiftsWhenTheRadioIsHandedBack)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtasticProfile);

    EXPECT_FALSE(meshtastic.vetoed());
    meshtastic.offer();
    EXPECT_EQ(meshtastic.transmitted(), 1u);
}

TEST_F(VetoTest, VetoHoldsAcrossADwellThatSpansManyPumps)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    driver.irq = true;

    for (int i = 0; i < 50; i++) {
        arbiter.pumpMeshcore();
        meshtastic.offer();
        EXPECT_TRUE(meshtastic.vetoed());
    }

    EXPECT_EQ(meshtastic.transmitted(), 0u);
    EXPECT_EQ(meshtastic.deferred(), 50u);
}

TEST_F(VetoTest, VetoHoldsWhileMeshcoreIsTransmitting)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    ASSERT_TRUE(arbiter.queueTx(reinterpret_cast<const uint8_t *>("out"), 3));
    arbiter.pumpMeshcore();

    ASSERT_TRUE(arbiter.meshcoreTxBusy());
    meshtastic.offer();
    EXPECT_EQ(meshtastic.transmitted(), 0u);
}

TEST_F(VetoTest, DeferredTransmitsAreNeverLost)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    for (int i = 0; i < 5; i++)
        meshtastic.offer();
    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtasticProfile);
    meshtastic.offer();

    EXPECT_EQ(meshtastic.offered(), 6u);
    EXPECT_EQ(meshtastic.deferred() + meshtastic.transmitted(), meshtastic.offered());
    EXPECT_EQ(meshtastic.transmitted(), 1u);
}

TEST_F(VetoTest, FailedEntryDoesNotLeaveTheVetoStuckOn)
{
    driver.configureFails = true;
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_FALSE(meshtastic.vetoed());
    meshtastic.offer();
    EXPECT_EQ(meshtastic.transmitted(), 1u);
}

TEST_F(VetoTest, SchedulerDrivenRunNeverTransmitsInsideASlice)
{
    SliceConfig config;
    SliceScheduler scheduler(arbiter, config);
    scheduler.setProfiles(meshtasticProfile, meshcore);

    uint32_t now = 0;
    uint32_t insideSlice = 0;

    for (int i = 0; i < 500; i++) {
        driver.cadDetected = (i % 6) == 0;
        const uint32_t delay = scheduler.tick(now);

        const bool held = arbiter.leaseHeld();
        meshtastic.offer();
        if (held && meshtastic.transmitted() > insideSlice)
            FAIL() << "Meshtastic transmitted while the bridge held the radio";
        if (!held)
            insideSlice = meshtastic.transmitted();

        now += delay == 0 ? 1 : delay;
    }

    EXPECT_GT(meshtastic.deferred(), 0u);
    EXPECT_GT(meshtastic.transmitted(), 0u);
    EXPECT_EQ(driver.ownershipViolationsSeen(), 0u);
}

TEST_F(VetoTest, WithoutTheLeaseTheGuardWouldNotHold)
{
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

    const bool guardActive = meshtastic.vetoed();
    ASSERT_TRUE(guardActive) << "the veto is the only thing preventing a transmit here";

    arbiter.leaveMeshcore(SwitchMode::Aligned, meshtasticProfile);
    EXPECT_FALSE(meshtastic.vetoed());
}
