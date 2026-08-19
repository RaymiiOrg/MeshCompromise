#include <gtest/gtest.h>

#include <cmath>

#include "meshcompromise/airtime.h"
#include "meshcompromise/lora_profile.h"
#include "meshcompromise/sx1262_driver.h"

#include "fake_radiolib_hal.h"

using namespace meshcompromise;

namespace
{

class AirtimeVsRadioLib : public ::testing::Test
{
  protected:
    AirtimeVsRadioLib() : module_(&hal_, 0, 1, 2, 3), radio_(&module_), driver_(radio_) {}

    float radioLibAirtimeMs(const LoraProfile &profile, uint16_t bytes)
    {
        EXPECT_TRUE(driver_.configure(profile, 17));
        return static_cast<float>(radio_.getTimeOnAir(bytes)) / 1000.0f;
    }

    FakeSx126xHal hal_;
    Module module_;
    SX1262 radio_;
    Sx1262Driver driver_;
};

void expectWithin(float ours, float theirs, float tolerance)
{
    EXPECT_NEAR(ours, theirs, tolerance) << "ours " << ours << "ms, RadioLib " << theirs << "ms";
}

} // namespace

TEST_F(AirtimeVsRadioLib, TheMeshcoreDefaultProfileAgrees)
{
    const LoraProfile profile = meshcoreDefaultProfile();

    for (uint16_t bytes : {16, 32, 64, 128, 184})
        expectWithin(packetAirtimeMs(profile, bytes), radioLibAirtimeMs(profile, bytes), 12.0f);
}

TEST_F(AirtimeVsRadioLib, TheMeshtasticNarrowSlowProfileAgrees)
{
    const LoraProfile profile = meshtasticNarrowSlowProfile();

    for (uint16_t bytes : {16, 64, 128, 237})
        expectWithin(packetAirtimeMs(profile, bytes), radioLibAirtimeMs(profile, bytes), 12.0f);
}

TEST_F(AirtimeVsRadioLib, LongFastLikeSettingsAgree)
{
    LoraProfile profile = meshcoreDefaultProfile();
    profile.spreadingFactor = 11;
    profile.bandwidthKhz = 250.0f;
    profile.codingRate = 5;

    for (uint16_t bytes : {16, 64, 128, 237})
        expectWithin(packetAirtimeMs(profile, bytes), radioLibAirtimeMs(profile, bytes), 60.0f);
}

TEST_F(AirtimeVsRadioLib, TheSpreadingFactorSweepAgrees)
{
    LoraProfile profile = meshcoreDefaultProfile();

    for (uint8_t sf = 7; sf <= 12; sf++) {
        profile.spreadingFactor = sf;
        const float ours = packetAirtimeMs(profile, 64);
        const float theirs = radioLibAirtimeMs(profile, 64);
        EXPECT_NEAR(ours, theirs, theirs * 0.05f) << "sf" << static_cast<int>(sf);
    }
}

TEST_F(AirtimeVsRadioLib, TheBandwidthSweepAgrees)
{
    LoraProfile profile = meshcoreDefaultProfile();

    for (float bw : {62.5f, 125.0f, 250.0f}) {
        profile.bandwidthKhz = bw;
        const float ours = packetAirtimeMs(profile, 64);
        const float theirs = radioLibAirtimeMs(profile, 64);
        EXPECT_NEAR(ours, theirs, theirs * 0.05f) << "bw" << bw;
    }
}

TEST_F(AirtimeVsRadioLib, WeAgreeOnWhenLowDataRateOptimiseKicksIn)
{
    LoraProfile slow = meshcoreDefaultProfile();
    slow.spreadingFactor = 12;
    slow.bandwidthKhz = 62.5f;
    EXPECT_TRUE(lowDataRateOptimize(slow.bandwidthKhz, slow.spreadingFactor));

    LoraProfile fast = meshcoreDefaultProfile();
    fast.spreadingFactor = 7;
    fast.bandwidthKhz = 250.0f;
    EXPECT_FALSE(lowDataRateOptimize(fast.bandwidthKhz, fast.spreadingFactor));

    const float slowOurs = packetAirtimeMs(slow, 64);
    const float slowTheirs = radioLibAirtimeMs(slow, 64);
    EXPECT_NEAR(slowOurs, slowTheirs, slowTheirs * 0.05f);
}

TEST_F(AirtimeVsRadioLib, ThePreambleTimeAgreesWithTheSymbolCount)
{
    LoraProfile profile = meshcoreDefaultProfile();
    profile.preambleSymbols = 32;

    const float shortAir = radioLibAirtimeMs(profile, 64);

    profile.preambleSymbols = 64;
    const float longAir = radioLibAirtimeMs(profile, 64);

    const float theirDelta = longAir - shortAir;

    LoraProfile a = meshcoreDefaultProfile();
    a.preambleSymbols = 32;
    LoraProfile b = a;
    b.preambleSymbols = 64;
    const float ourDelta = preambleTimeMs(b) - preambleTimeMs(a);

    EXPECT_NEAR(ourDelta, theirDelta, 2.0f);
}

TEST_F(AirtimeVsRadioLib, OurScanPeriodStaysInsideARealPreamble)
{
    const LoraProfile profile = meshcoreDefaultProfile();

    EXPECT_TRUE(driver_.configure(profile, 17));
    const float preambleOnly = static_cast<float>(radio_.getTimeOnAir(0)) / 1000.0f;

    EXPECT_LT(static_cast<float>(recommendedScanPeriodMs(profile)), preambleOnly);
    EXPECT_LT(static_cast<float>(maxScanPeriodMs(profile)), preambleOnly);
}
