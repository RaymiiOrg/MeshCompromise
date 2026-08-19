#include <gtest/gtest.h>

#include "meshcompromise/airtime.h"
#include "meshcompromise/lora_profile.h"

using namespace meshcompromise;

namespace
{

constexpr float kSplitSwitchBudgetMs = 6.0f;

LoraProfile meshtasticPreset(uint8_t spreadingFactor, float bandwidthKhz)
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.spreadingFactor = spreadingFactor;
    profile.bandwidthKhz = bandwidthKhz;
    profile.codingRate = 5;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

LoraProfile longFast() { return meshtasticPreset(11, 250.0f); }
LoraProfile mediumFast() { return meshtasticPreset(9, 250.0f); }
LoraProfile narrowSlow() { return meshtasticPreset(8, 62.5f); }

} // namespace

TEST(CaptureBudget, OurShippedProfileMatchesTheRealCardputerMeshcoreBuild)
{
    const LoraProfile meshcore = meshcoreCardputerProfile();

    EXPECT_FLOAT_EQ(869.525f, meshcore.frequencyMhz);
    EXPECT_FLOAT_EQ(250.0f, meshcore.bandwidthKhz);
    EXPECT_EQ(11u, meshcore.spreadingFactor);
    EXPECT_EQ(5u, meshcore.codingRate);
    EXPECT_EQ(16u, meshcore.preambleSymbols);
}

TEST(CaptureBudget, TheUpstreamMeshcoreBuildFlagsAreStillTheNamedDefault)
{
    const LoraProfile upstream = meshcoreDefaultProfile();

    EXPECT_FLOAT_EQ(869.618f, upstream.frequencyMhz);
    EXPECT_FLOAT_EQ(62.5f, upstream.bandwidthKhz);
    EXPECT_EQ(8u, upstream.spreadingFactor);
    EXPECT_EQ(32u, upstream.preambleSymbols);
}

TEST(CaptureBudget, MeshtasticLongFastAndTheCardputerMeshcoreBuildDifferOnlyBySyncWord)
{
    const LoraProfile meshcore = meshcoreCardputerProfile();
    const LoraProfile longfast = meshtasticLongFastProfile();

    EXPECT_FLOAT_EQ(longfast.frequencyMhz, meshcore.frequencyMhz);
    EXPECT_FLOAT_EQ(longfast.bandwidthKhz, meshcore.bandwidthKhz);
    EXPECT_EQ(longfast.spreadingFactor, meshcore.spreadingFactor);
    EXPECT_EQ(longfast.codingRate, meshcore.codingRate);
    EXPECT_EQ(longfast.preambleSymbols, meshcore.preambleSymbols);
    EXPECT_NE(longfast.syncWord, meshcore.syncWord);
}

TEST(CaptureBudget, StockEuLongFastReachesAlignedModeWithNoUserChanges)
{
    EXPECT_EQ(SwitchMode::Aligned, selectSwitchMode(meshtasticLongFastProfile(), meshcoreCardputerProfile()));
    EXPECT_EQ(AlignmentBlocker::None, alignmentBlocker(meshtasticLongFastProfile(), meshcoreCardputerProfile()));
}

TEST(CaptureBudget, LongFastPreambleMatchesUpstreamsDocumentedValue)
{
    EXPECT_NEAR(165.0f, preambleTimeMs(longFast()), 1.0f);
}

TEST(CaptureBudget, WeReturnToMeshcoreBeforeItsPreambleEnds)
{
    const LoraProfile meshcore = meshcoreDefaultProfile();
    EXPECT_LT(static_cast<float>(recommendedScanPeriodMs(meshcore)), preambleTimeMs(meshcore));
}

TEST(CaptureBudget, TheSlowerMeshtasticPresetsOutlastOneScanExcursion)
{
    const LoraProfile meshcore = meshcoreDefaultProfile();
    const float excursion = static_cast<float>(minimumDetectDwellMs(meshcore)) + 2.0f * kSplitSwitchBudgetMs;

    EXPECT_LT(excursion, preambleTimeMs(longFast()));
    EXPECT_LT(excursion, preambleTimeMs(narrowSlow()));
}

TEST(CaptureBudget, MediumFastCannotAbsorbOneScanExcursion)
{
    const LoraProfile meshcore = meshcoreDefaultProfile();
    const float excursion = static_cast<float>(minimumDetectDwellMs(meshcore)) + 2.0f * kSplitSwitchBudgetMs;

    EXPECT_GT(excursion, preambleTimeMs(mediumFast()));
    EXPECT_FALSE(hostToleratesScanning(mediumFast(), meshcore, SwitchMode::Split));
}

TEST(CaptureBudget, LongFastIsAnEasierCaptureTargetThanTheUpstreamMeshcorePhy)
{
    EXPECT_GT(preambleTimeMs(longFast()), preambleTimeMs(meshcoreDefaultProfile()));
}

TEST(CaptureBudget, NarrowSlowIsTheTightestMeshtasticPreamble)
{
    EXPECT_LT(preambleTimeMs(narrowSlow()), preambleTimeMs(longFast()));
    EXPECT_GT(preambleTimeMs(narrowSlow()), preambleTimeMs(mediumFast()));
}

TEST(CaptureBudget, PacketAirtimeDominatesTheSwitchingCost)
{
    const LoraProfile meshcore = meshcoreDefaultProfile();
    EXPECT_GT(packetAirtimeMs(meshcore, 100), 100.0f * kSplitSwitchBudgetMs);
    EXPECT_GT(packetAirtimeMs(longFast(), 60), 100.0f * kSplitSwitchBudgetMs);
}

TEST(CaptureBudget, AgainstTheUpstreamPhyLongFastFallsBackToSplit)
{
    const LoraProfile upstream = meshcoreDefaultProfile();

    EXPECT_EQ(SwitchMode::Split, selectSwitchMode(longFast(), upstream));

    LoraProfile aligned = narrowSlow();
    aligned.frequencyMhz = upstream.frequencyMhz;
    EXPECT_EQ(SwitchMode::Aligned, selectSwitchMode(aligned, upstream));
}
