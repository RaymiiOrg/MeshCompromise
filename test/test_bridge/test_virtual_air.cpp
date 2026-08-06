#include <gtest/gtest.h>

#include "meshcompromise/radio_arbiter.h"
#include "virtual_air.h"

using namespace meshcompromise;

namespace
{

LoraProfile longFast()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.bandwidthKhz = 250.0f;
    profile.spreadingFactor = 11;
    profile.codingRate = 5;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

class AirTest : public ::testing::Test
{
  protected:
    VirtualAir air;
    FakeSxDriver driver;
    FakeHostRadio host;
    RadioArbiter arbiter{driver, host};

    LoraProfile meshcore = meshcoreDefaultProfile();
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
    std::vector<uint8_t> frame{0x15, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};

    void SetUp() override { driver.active = meshtastic; }
};

} // namespace

TEST_F(AirTest, IdenticalProfilesAreHearable)
{
    EXPECT_TRUE(VirtualAir::hearable(meshcore, meshcore));
}

TEST_F(AirTest, SyncWordAloneBlocksDelivery)
{
    LoraProfile impostor = meshcore;
    impostor.syncWord = 0x2b;
    EXPECT_FALSE(VirtualAir::hearable(impostor, meshcore));
}

TEST_F(AirTest, MeshtasticNarrowSlowCannotHearMeshcoreDespiteMatchingSfAndBw)
{
    EXPECT_EQ(meshtastic.spreadingFactor, meshcore.spreadingFactor);
    EXPECT_FLOAT_EQ(meshtastic.bandwidthKhz, meshcore.bandwidthKhz);
    EXPECT_FALSE(VirtualAir::hearable(meshcore, meshtastic));
}

TEST_F(AirTest, LongFastCannotHearMeshcore)
{
    EXPECT_FALSE(VirtualAir::hearable(meshcore, longFast()));
}

TEST_F(AirTest, SpreadingFactorMismatchBlocksDelivery)
{
    LoraProfile other = meshcore;
    other.spreadingFactor = 9;
    EXPECT_FALSE(VirtualAir::hearable(other, meshcore));
}

TEST_F(AirTest, BandwidthMismatchBlocksDelivery)
{
    LoraProfile other = meshcore;
    other.bandwidthKhz = 125.0f;
    EXPECT_FALSE(VirtualAir::hearable(other, meshcore));
}

TEST_F(AirTest, SmallFrequencyOffsetIsStillHearable)
{
    LoraProfile other = meshcore;
    other.frequencyMhz = meshcore.frequencyMhz + 0.010f;
    EXPECT_TRUE(VirtualAir::hearable(other, meshcore));
}

TEST_F(AirTest, LargeFrequencyOffsetIsNotHearable)
{
    LoraProfile other = meshcore;
    other.frequencyMhz = meshcore.frequencyMhz + 0.050f;
    EXPECT_FALSE(VirtualAir::hearable(other, meshcore));
}

TEST_F(AirTest, FrameArrivingDuringAMeshcoreSliceIsReceived)
{
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    ASSERT_TRUE(air.deliverTo(driver, meshcore, frame));
    arbiter.pumpMeshcore();

    ASSERT_TRUE(arbiter.rxAvailable());
    uint8_t out[64] = {0};
    const size_t length = arbiter.takeRx(out, sizeof(out));
    EXPECT_EQ(std::vector<uint8_t>(out, out + length), frame);
}

TEST_F(AirTest, FrameArrivingWhileMeshtasticOwnsTheRadioIsMissed)
{
    driver.active = meshtastic;

    EXPECT_FALSE(air.deliverTo(driver, meshcore, frame));
    EXPECT_EQ(air.missed, 1u);
    EXPECT_FALSE(arbiter.rxAvailable());
}

TEST_F(AirTest, MeshtasticTrafficIsNotMisreadAsMeshcore)
{
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);

    EXPECT_FALSE(air.deliverTo(driver, longFast(), frame));
    arbiter.pumpMeshcore();
    EXPECT_FALSE(arbiter.rxAvailable());
}

TEST_F(AirTest, CaptureRateAcrossAlternatingSlicesIsMeasured)
{
    const int rounds = 40;
    int captured = 0;

    for (int i = 0; i < rounds; i++) {
        arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);

        if (air.deliverTo(driver, meshcore, frame)) {
            arbiter.pumpMeshcore();
            if (arbiter.rxAvailable()) {
                uint8_t sink[64];
                arbiter.takeRx(sink, sizeof(sink));
                captured++;
            }
        }

        arbiter.leaveMeshcore(SwitchMode::Aligned, meshtastic);
        driver.active = meshtastic;
    }

    EXPECT_EQ(captured, rounds);
    EXPECT_EQ(air.missed, 0u);
}

TEST_F(AirTest, AlignedModeRequiresTheHostToHaveSetTheSharedParameters)
{
    driver.active = LoraProfile();
    arbiter.enterMeshcore(SwitchMode::Aligned, meshcore);
    EXPECT_FALSE(air.deliverTo(driver, meshcore, frame));
}

TEST_F(AirTest, SplitModeDoesNotDependOnTheHostConfiguration)
{
    driver.active = LoraProfile();
    arbiter.enterMeshcore(SwitchMode::Split, meshcore);
    EXPECT_TRUE(air.deliverTo(driver, meshcore, frame));
}

TEST_F(AirTest, NothingIsCapturedWhenTheBridgeNeverTakesASlice)
{
    driver.active = meshtastic;
    int captured = 0;

    for (int i = 0; i < 40; i++) {
        if (air.deliverTo(driver, meshcore, frame)) {
            arbiter.pumpMeshcore();
            if (arbiter.rxAvailable())
                captured++;
        }
    }

    EXPECT_EQ(captured, 0);
    EXPECT_EQ(air.missed, 40u);
}
