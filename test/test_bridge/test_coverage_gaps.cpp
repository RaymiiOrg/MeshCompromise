#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/mirror.h"
#include "meshcompromise/radio_arbiter.h"
#include "meshcompromise/sx1262_driver.h"

#include "fake_radiolib_hal.h"
#include "fake_sx_driver.h"

using namespace meshcompromise;

namespace
{

class DriverGaps : public ::testing::Test
{
  protected:
    DriverGaps() : module_(&hal_, 0, 1, 2, 3), radio_(&module_), driver_(radio_) {}

    void SetUp() override
    {
        ASSERT_TRUE(driver_.begin());
        ASSERT_TRUE(driver_.configure(meshcoreDefaultProfile(), 17));
        hal_.clear();
    }

    FakeSx126xHal hal_;
    Module module_;
    SX1262 radio_;
    Sx1262Driver driver_;
};

} // namespace

TEST_F(DriverGaps, ChannelActivityIsReportedWhenTheChipDetectsIt)
{
    hal_.irqFlags = RADIOLIB_SX126X_IRQ_CAD_DETECTED | RADIOLIB_SX126X_IRQ_CAD_DONE;
    hal_.irqPinHigh = true;

    EXPECT_EQ(CadResult::Detected, driver_.scanChannel());
}

TEST_F(DriverGaps, AQuietChannelIsReportedAsFree)
{
    hal_.irqFlags = RADIOLIB_SX126X_IRQ_CAD_DONE;
    hal_.irqPinHigh = true;

    EXPECT_EQ(CadResult::Free, driver_.scanChannel());
}

TEST_F(DriverGaps, ThePacketLengthComesFromTheChip)
{
    hal_.rxBuffer = {1, 2, 3, 4, 5, 6, 7};

    EXPECT_EQ(7u, driver_.packetLength());
}

TEST_F(DriverGaps, AReceivedPacketIsReadOutOfTheChipBuffer)
{
    hal_.rxBuffer = {0xC0, 0xFF, 0xEE, 0x42};

    uint8_t out[4] = {0};
    ASSERT_TRUE(driver_.readPacket(out, sizeof(out)));

    EXPECT_EQ(0xC0, out[0]);
    EXPECT_EQ(0xFF, out[1]);
    EXPECT_EQ(0xEE, out[2]);
    EXPECT_EQ(0x42, out[3]);
}

TEST_F(DriverGaps, FinishingATransmitReachesTheChip)
{
    const uint8_t frame[3] = {1, 2, 3};
    ASSERT_TRUE(driver_.startTransmit(frame, sizeof(frame)));
    hal_.clear();

    EXPECT_TRUE(driver_.finishTransmit());
}

TEST_F(DriverGaps, AttachingAndDetachingInterruptsIsHarmlessWithoutAnAction)
{
    driver_.attachRxIrq();
    driver_.attachTxIrq();
    driver_.detachIrq();

    SUCCEED();
}

TEST_F(DriverGaps, SignalQualityIsReadFromTheChip)
{
    hal_.rxBuffer = {1, 2, 3};

    const float rssi = driver_.lastRssi();
    const float snr = driver_.lastSnr();

    EXPECT_TRUE(rssi <= 0.0f);
    EXPECT_TRUE(snr > -200.0f && snr < 200.0f);
}

TEST_F(DriverGaps, AChipThatRejectsACommandIsReportedNotSwallowed)
{
    hal_.failOpcode = RADIOLIB_SX126X_CMD_SET_STANDBY;

    EXPECT_FALSE(driver_.standby());
    EXPECT_NE(RADIOLIB_ERR_NONE, driver_.lastError());
}

TEST_F(DriverGaps, AFailedReconfigureIsReportedNotSwallowed)
{
    hal_.failOpcode = RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY;

    EXPECT_FALSE(driver_.configure(meshtasticNarrowSlowProfile(), 20));
    EXPECT_NE(RADIOLIB_ERR_NONE, driver_.lastError());
}

TEST(ProfileEquality, IdenticalProfilesCompareEqual)
{
    const LoraProfile a = meshcoreDefaultProfile();
    const LoraProfile b = meshcoreDefaultProfile();

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(ProfileEquality, EachFieldBreaksEquality)
{
    const LoraProfile base = meshcoreDefaultProfile();

    LoraProfile freq = base;
    freq.frequencyMhz += 1.0f;
    EXPECT_TRUE(freq != base);

    LoraProfile bw = base;
    bw.bandwidthKhz = 250.0f;
    EXPECT_TRUE(bw != base);

    LoraProfile sf = base;
    sf.spreadingFactor = 11;
    EXPECT_TRUE(sf != base);

    LoraProfile cr = base;
    cr.codingRate = 8;
    EXPECT_TRUE(cr != base);

    LoraProfile sync = base;
    sync.syncWord = 0x2b;
    EXPECT_TRUE(sync != base);

    LoraProfile preamble = base;
    preamble.preambleSymbols = 64;
    EXPECT_TRUE(preamble != base);
}

TEST(ProfileEquality, ATinyFrequencyDifferenceIsStillTheSameProfile)
{
    const LoraProfile base = meshcoreDefaultProfile();
    LoraProfile nudged = base;
    nudged.frequencyMhz += 0.000001f;

    EXPECT_TRUE(nudged == base);
}

TEST(SwitchModeSelection, ADifferentFrequencyForcesSplit)
{
    LoraProfile meshtastic = meshcoreDefaultProfile();
    LoraProfile meshcore = meshcoreDefaultProfile();
    meshcore.frequencyMhz += 5.0f;

    EXPECT_EQ(SwitchMode::Split, selectSwitchMode(meshtastic, meshcore));
}

TEST(SwitchModeSelection, ADifferentCodingRateStillAllowsAligned)
{
    LoraProfile meshtastic = meshcoreDefaultProfile();
    LoraProfile meshcore = meshcoreDefaultProfile();
    meshcore.codingRate = 8;

    EXPECT_EQ(SwitchMode::Aligned, selectSwitchMode(meshtastic, meshcore));
}

TEST(MirrorBookkeeping, ClearingTheHistoryForgetsEverything)
{
    MirrorHistory history;
    history.record(1);
    history.record(2);
    ASSERT_TRUE(history.contains(1));

    history.clear();

    EXPECT_EQ(0u, history.size());
    EXPECT_FALSE(history.contains(1));
    EXPECT_FALSE(history.contains(2));
}

TEST(MirrorBookkeeping, ResettingCountersDoesNotForgetTheHistory)
{
    Mirror mirror{MirrorConfig()};
    mirror.setLocalNode(0xAABBCCDD);

    MirrorSource source;
    source.packetId = 7;
    source.fromNode = 0xAABBCCDD;
    source.toNode = 0xFFFFFFFFu;
    source.isTextMessage = true;
    source.isBroadcast = true;

    MirrorMessage message;
    ASSERT_EQ(MirrorDecision::Send, mirror.prepare(source, "once", 4, message));
    ASSERT_EQ(1u, mirror.mirroredCount());

    mirror.resetCounters();

    EXPECT_EQ(0u, mirror.mirroredCount());
    EXPECT_EQ(MirrorDecision::AlreadyMirrored, mirror.prepare(source, "once", 4, message));
}

TEST(ArbiterGaps, TransmitPowerIsClampedToWhatTheChipAccepts)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter(driver, host);

    arbiter.setTxPower(-40);
    arbiter.enterMeshcore(SwitchMode::Split, meshcoreDefaultProfile());
    EXPECT_GE(driver.lastTxPower, -9);

    arbiter.leaveMeshcore(SwitchMode::Split, meshcoreDefaultProfile());
    arbiter.setTxPower(40);
    arbiter.enterMeshcore(SwitchMode::Split, meshcoreDefaultProfile());
    EXPECT_LE(driver.lastTxPower, 22);
}

TEST(ArbiterGaps, ReleasingARadioWeNeverTookIsANoOp)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter(driver, host);

    arbiter.leaveMeshcore(SwitchMode::Aligned, meshcoreDefaultProfile());

    EXPECT_EQ(0, host.restores);
    EXPECT_FALSE(driver.didOp("standby"));
}

TEST(ArbiterGaps, SettingTheProfileBeforeASliceIsUsedByThatSlice)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter(driver, host);

    LoraProfile custom = meshcoreDefaultProfile();
    custom.syncWord = 0x77;
    arbiter.setMeshcoreProfile(custom);

    arbiter.enterMeshcore(SwitchMode::Aligned, custom);

    EXPECT_EQ(0x77, driver.active.syncWord);
}

TEST(ArbiterGaps, AChipThatRefusesToTransmitCountsADrop)
{
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter(driver, host);

    arbiter.enterMeshcore(SwitchMode::Split, meshcoreDefaultProfile());

    const uint8_t frame[4] = {1, 2, 3, 4};
    ASSERT_TRUE(arbiter.queueTx(frame, sizeof(frame)));

    driver.transmitFails = true;
    arbiter.pumpMeshcore();

    EXPECT_GT(arbiter.txDropped(), 0u);
}
