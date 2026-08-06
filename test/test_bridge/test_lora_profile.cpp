#include <gtest/gtest.h>

#include "meshcompromise/lora_profile.h"

using namespace meshcompromise;

TEST(LoraProfile, MeshcoreDefaultsMatchUpstreamBuildFlags)
{
    const LoraProfile profile = meshcoreDefaultProfile();
    EXPECT_FLOAT_EQ(profile.frequencyMhz, 869.618f);
    EXPECT_FLOAT_EQ(profile.bandwidthKhz, 62.5f);
    EXPECT_EQ(profile.spreadingFactor, 8);
    EXPECT_EQ(profile.codingRate, 5);
    EXPECT_EQ(profile.syncWord, 0x12);
}

TEST(LoraProfile, MeshtasticNarrowSlowMatchesPresetTable)
{
    const LoraProfile profile = meshtasticNarrowSlowProfile();
    EXPECT_FLOAT_EQ(profile.bandwidthKhz, 62.5f);
    EXPECT_EQ(profile.spreadingFactor, 8);
    EXPECT_EQ(profile.codingRate, 6);
    EXPECT_EQ(profile.syncWord, 0x2b);
}

TEST(LoraProfile, PreambleSymbolsFollowSpreadingFactor)
{
    EXPECT_EQ(meshcorePreambleSymbols(7), 32);
    EXPECT_EQ(meshcorePreambleSymbols(8), 32);
    EXPECT_EQ(meshcorePreambleSymbols(9), 16);
    EXPECT_EQ(meshcorePreambleSymbols(12), 16);
}

TEST(LoraProfile, ToleranceIsQuarterBandwidth)
{
    EXPECT_FLOAT_EQ(frequencyToleranceKhz(62.5f), 15.625f);
    EXPECT_FLOAT_EQ(frequencyToleranceKhz(250.0f), 62.5f);
    EXPECT_FLOAT_EQ(frequencyToleranceKhz(0.0f), 0.0f);
}

TEST(LoraProfile, NarrowSlowAndMeshcoreDefaultAreAligned)
{
    EXPECT_EQ(selectSwitchMode(meshtasticNarrowSlowProfile(), meshcoreDefaultProfile()), SwitchMode::Aligned);
}

TEST(LoraProfile, TenKilohertzOffsetStaysWithinTolerance)
{
    EXPECT_TRUE(frequenciesInterchangeable(869.60825f, 869.618f, 62.5f));
}

TEST(LoraProfile, TwentyKilohertzOffsetExceedsTolerance)
{
    EXPECT_FALSE(frequenciesInterchangeable(869.598f, 869.618f, 62.5f));
}

TEST(LoraProfile, DifferentSpreadingFactorForcesSplit)
{
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
    meshtastic.spreadingFactor = 11;
    EXPECT_EQ(selectSwitchMode(meshtastic, meshcoreDefaultProfile()), SwitchMode::Split);
}

TEST(LoraProfile, DifferentBandwidthForcesSplit)
{
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
    meshtastic.bandwidthKhz = 250.0f;
    EXPECT_EQ(selectSwitchMode(meshtastic, meshcoreDefaultProfile()), SwitchMode::Split);
}

TEST(LoraProfile, LongFastAgainstMeshcoreDefaultIsSplit)
{
    LoraProfile longFast;
    longFast.frequencyMhz = 869.525f;
    longFast.bandwidthKhz = 250.0f;
    longFast.spreadingFactor = 11;
    longFast.codingRate = 5;
    longFast.syncWord = 0x2b;
    longFast.preambleSymbols = 16;
    EXPECT_EQ(selectSwitchMode(longFast, meshcoreDefaultProfile()), SwitchMode::Split);
}

TEST(LoraProfile, CodingRateDifferenceDoesNotForceSplit)
{
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
    LoraProfile meshcore = meshcoreDefaultProfile();
    meshtastic.frequencyMhz = meshcore.frequencyMhz;
    EXPECT_NE(meshtastic.codingRate, meshcore.codingRate);
    EXPECT_EQ(selectSwitchMode(meshtastic, meshcore), SwitchMode::Aligned);
}

TEST(LoraProfile, SyncWordDifferenceDoesNotForceSplit)
{
    LoraProfile meshtastic = meshtasticNarrowSlowProfile();
    LoraProfile meshcore = meshcoreDefaultProfile();
    meshtastic.frequencyMhz = meshcore.frequencyMhz;
    EXPECT_NE(meshtastic.syncWord, meshcore.syncWord);
    EXPECT_EQ(selectSwitchMode(meshtastic, meshcore), SwitchMode::Aligned);
}

TEST(LoraProfile, UninitialisedProfileIsSplit)
{
    LoraProfile empty;
    EXPECT_EQ(selectSwitchMode(empty, meshcoreDefaultProfile()), SwitchMode::Split);
}
