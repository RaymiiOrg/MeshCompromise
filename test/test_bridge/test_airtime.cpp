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

TEST(Airtime, DetectDwellMatchesTheRadioLibCadSymbolCount)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_GE(minimumDetectDwellMs(profile), static_cast<uint32_t>(symbolTimeMs(62.5f, 8) * 8.0f));
    EXPECT_LT(minimumDetectDwellMs(profile), static_cast<uint32_t>(symbolTimeMs(62.5f, 8) * 9.0f));
}

TEST(Airtime, TheScanPeriodCeilingStillCoversThePreamble)
{
    for (uint8_t sf = 5; sf <= 12; sf++) {
        LoraProfile profile = meshcoreDefaultProfile();
        profile.spreadingFactor = sf;
        profile.preambleSymbols = meshcorePreambleSymbols(sf);

        const uint32_t ceiling = maxScanPeriodMs(profile);
        EXPECT_GT(ceiling, 0u) << "sf " << static_cast<int>(sf);
        EXPECT_TRUE(scanPeriodCoversPreamble(ceiling, profile)) << "sf " << static_cast<int>(sf);
    }
}

TEST(Airtime, TheScanPeriodCeilingIsNeverBelowTheRecommendedPeriod)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_GE(maxScanPeriodMs(profile), recommendedScanPeriodMs(profile));
}

TEST(Airtime, PacketScoreIsZeroBelowTheMeshcoreSnrThreshold)
{
    EXPECT_FLOAT_EQ(packetScore(-11.0f, 8, 64), 0.0f);
    EXPECT_FLOAT_EQ(packetScore(-21.0f, 12, 64), 0.0f);
}

TEST(Airtime, PacketScoreRisesWithSnr)
{
    EXPECT_LT(packetScore(-5.0f, 8, 64), packetScore(0.0f, 8, 64));
}

TEST(Airtime, PacketScoreFallsWithPacketLength)
{
    EXPECT_GT(packetScore(0.0f, 8, 32), packetScore(0.0f, 8, 200));
}

TEST(Airtime, PacketScoreStaysWithinZeroToOne)
{
    for (uint8_t sf = 7; sf <= 12; sf++) {
        for (int len = 0; len <= 256; len += 32) {
            for (float snr = -25.0f; snr <= 20.0f; snr += 2.5f) {
                const float score = packetScore(snr, sf, len);
                EXPECT_GE(score, 0.0f);
                EXPECT_LE(score, 1.0f);
            }
        }
    }
}

TEST(Airtime, PacketScoreRejectsSpreadingFactorsMeshcoreDoesNotScore)
{
    EXPECT_FLOAT_EQ(packetScore(10.0f, 6, 64), 0.0f);
    EXPECT_FLOAT_EQ(packetScore(10.0f, 13, 64), 0.0f);
}

TEST(Airtime, PacketScoreMatchesMeshcoreAtTheThresholdBoundary)
{
    EXPECT_FLOAT_EQ(packetScore(-10.0f, 8, 0), 0.0f);
    EXPECT_GT(packetScore(-9.9f, 8, 0), 0.0f);
}

TEST(Airtime, ScanPeriodDefinedForSlowerProfiles)
{
    LoraProfile slow = meshcoreDefaultProfile();
    slow.spreadingFactor = 11;
    slow.preambleSymbols = meshcorePreambleSymbols(11);
    EXPECT_TRUE(scanPeriodCoversPreamble(recommendedScanPeriodMs(slow), slow));
}
