#include <gtest/gtest.h>

#include "meshcompromise/airtime.h"

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
} // namespace

TEST(Airtime, SymbolTimeMatchesSemtechFormula)
{
    EXPECT_NEAR(symbolTimeMs(62.5f, 8), 4.096f, 0.001f);
    EXPECT_NEAR(symbolTimeMs(250.0f, 11), 8.192f, 0.001f);
    EXPECT_NEAR(symbolTimeMs(125.0f, 12), 32.768f, 0.001f);
}

TEST(Airtime, ZeroBandwidthIsSafe)
{
    EXPECT_FLOAT_EQ(symbolTimeMs(0.0f, 8), 0.0f);
    EXPECT_FLOAT_EQ(symbolTimeMs(62.5f, 0), 0.0f);
}

TEST(Airtime, LowDataRateOptimizeEngagesAtSixteenMilliseconds)
{
    EXPECT_FALSE(lowDataRateOptimize(250.0f, 11));
    EXPECT_TRUE(lowDataRateOptimize(125.0f, 12));
}

TEST(Airtime, MeshcorePreambleIsAboutOneHundredFortyEightMilliseconds)
{
    EXPECT_NEAR(preambleTimeMs(meshcoreDefaultProfile()), 148.5f, 1.0f);
}

TEST(Airtime, LongFastPreambleIsAboutOneHundredSixtySixMilliseconds)
{
    EXPECT_NEAR(preambleTimeMs(longFast()), 165.9f, 1.0f);
}

TEST(Airtime, MeshcoreMaxPayloadAirtimeIsAboutOnePointOneSeconds)
{
    EXPECT_NEAR(packetAirtimeMs(meshcoreDefaultProfile(), 184), 1140.0f, 40.0f);
}

TEST(Airtime, LongFastFullPacketAirtimeIsAboutTwoSeconds)
{
    EXPECT_NEAR(packetAirtimeMs(longFast(), 249), 2120.0f, 80.0f);
}

TEST(Airtime, AirtimeGrowsWithPayload)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_LT(packetAirtimeMs(profile, 20), packetAirtimeMs(profile, 100));
    EXPECT_LT(packetAirtimeMs(profile, 100), packetAirtimeMs(profile, 184));
}

TEST(Airtime, ScanPeriodFitsInsideMeshcorePreamble)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    const uint32_t period = recommendedScanPeriodMs(profile);
    EXPECT_GT(period, 0u);
    EXPECT_LT(static_cast<float>(period), preambleTimeMs(profile));
    EXPECT_TRUE(scanPeriodCoversPreamble(period, profile));
}

TEST(Airtime, OversizedScanPeriodIsRejected)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_FALSE(scanPeriodCoversPreamble(200, profile));
}

TEST(Airtime, DetectDwellCoversEightSymbols)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_GE(minimumDetectDwellMs(profile), static_cast<uint32_t>(symbolTimeMs(62.5f, 8) * 8.0f));
}

TEST(Airtime, ScanPeriodDefinedForSlowerProfiles)
{
    LoraProfile slow = meshcoreDefaultProfile();
    slow.spreadingFactor = 11;
    slow.preambleSymbols = meshcorePreambleSymbols(11);
    EXPECT_TRUE(scanPeriodCoversPreamble(recommendedScanPeriodMs(slow), slow));
}
