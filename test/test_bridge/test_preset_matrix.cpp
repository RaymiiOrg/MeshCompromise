#include <gtest/gtest.h>

#include <string>

#include "meshcompromise/airtime.h"
#include "meshcompromise/lora_profile.h"
#include "meshcompromise/slice_scheduler.h"

#include "fake_radio.h"

using namespace meshcompromise;

namespace
{

struct Preset {
    const char *name;
    float frequencyMhz;
    uint8_t spreadingFactor;
    float bandwidthKhz;
    uint8_t codingRate;
};

const Preset kMeshcorePresets[] = {
    {"New Zealand", 917.375f, 11, 250.0f, 5},        {"New Zealand (Nrrw)", 917.375f, 7, 62.5f, 5},
    {"Portugal 868", 869.618f, 7, 62.5f, 6},         {"Switzerland", 869.618f, 8, 62.5f, 8},
    {"USA / Canada (Rec)", 910.525f, 7, 62.5f, 5},   {"Vietnam", 920.250f, 11, 250.0f, 5},
    {"Australia", 915.800f, 10, 250.0f, 5},          {"Australia (Narrow)", 916.575f, 7, 62.5f, 8},
    {"Australia: SA, WA", 923.125f, 8, 62.5f, 8},    {"Australia: QLD", 923.125f, 8, 62.5f, 5},
    {"EU / UK (Narrow)", 869.618f, 8, 62.5f, 8},     {"EU / UK (Long Rng)", 869.525f, 11, 250.0f, 5},
    {"EU / UK (Med Rng)", 869.525f, 10, 250.0f, 5},  {"Czech Rep (Narrow)", 869.432f, 7, 62.5f, 5},
};

const Preset kMeshtasticPresets[] = {
    {"ShortTurbo", 0.0f, 7, 500.0f, 5},   {"ShortFast", 0.0f, 7, 250.0f, 5},
    {"ShortSlow", 0.0f, 8, 250.0f, 5},    {"MediumFast", 0.0f, 9, 250.0f, 5},
    {"MediumSlow", 0.0f, 10, 250.0f, 5},  {"MediumTurbo", 0.0f, 9, 500.0f, 5},
    {"LongTurbo", 0.0f, 11, 500.0f, 8},   {"LongFast", 0.0f, 11, 250.0f, 5},
    {"LongModerate", 0.0f, 11, 125.0f, 8}, {"LongSlow", 0.0f, 12, 125.0f, 8},
    {"LiteFast", 0.0f, 9, 125.0f, 5},     {"LiteSlow", 0.0f, 10, 125.0f, 5},
    {"NarrowFast", 0.0f, 7, 62.5f, 6},    {"NarrowSlow", 0.0f, 8, 62.5f, 6},
    {"TinyFast", 0.0f, 7, 15.6f, 5},      {"TinySlow", 0.0f, 8, 15.6f, 6},
};

constexpr size_t kMeshcoreCount = sizeof(kMeshcorePresets) / sizeof(kMeshcorePresets[0]);
constexpr size_t kMeshtasticCount = sizeof(kMeshtasticPresets) / sizeof(kMeshtasticPresets[0]);

LoraProfile meshcoreOf(const Preset &preset)
{
    LoraProfile profile;
    profile.frequencyMhz = preset.frequencyMhz;
    profile.spreadingFactor = preset.spreadingFactor;
    profile.bandwidthKhz = preset.bandwidthKhz;
    profile.codingRate = preset.codingRate;
    profile.syncWord = 0x12;
    profile.preambleSymbols = meshcorePreambleSymbols(preset.spreadingFactor);
    return profile;
}

LoraProfile meshtasticOf(const Preset &preset, float frequencyMhz)
{
    LoraProfile profile;
    profile.frequencyMhz = frequencyMhz;
    profile.spreadingFactor = preset.spreadingFactor;
    profile.bandwidthKhz = preset.bandwidthKhz;
    profile.codingRate = preset.codingRate;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

} // namespace

TEST(PresetMatrix, EveryMeshcorePresetHasAWorkableSchedule)
{
    for (const Preset &preset : kMeshcorePresets) {
        const LoraProfile profile = meshcoreOf(preset);
        SCOPED_TRACE(preset.name);

        EXPECT_GT(preambleTimeMs(profile), 0.0f);
        EXPECT_GT(packetAirtimeMs(profile, 100), 0.0f);
        EXPECT_GT(recommendedScanPeriodMs(profile), 0u);
        EXPECT_GT(minimumDetectDwellMs(profile), 0u);
        EXPECT_LT(static_cast<float>(recommendedScanPeriodMs(profile)), preambleTimeMs(profile));
        EXPECT_TRUE(scanPeriodCoversPreamble(recommendedScanPeriodMs(profile), profile));
    }
}

TEST(PresetMatrix, EveryMeshcorePresetHasExactlyOneAlignedMeshtasticPreset)
{
    for (const Preset &preset : kMeshcorePresets) {
        const LoraProfile meshcore = meshcoreOf(preset);
        SCOPED_TRACE(preset.name);

        size_t aligned = 0;
        for (const Preset &host : kMeshtasticPresets)
            if (selectSwitchMode(meshtasticOf(host, preset.frequencyMhz), meshcore) == SwitchMode::Aligned)
                aligned++;

        EXPECT_EQ(1u, aligned);
    }
}

TEST(PresetMatrix, TheAlignedPartnerIsTheOneSharingSpreadingFactorAndBandwidth)
{
    for (const Preset &preset : kMeshcorePresets) {
        const LoraProfile meshcore = meshcoreOf(preset);
        SCOPED_TRACE(preset.name);

        for (const Preset &host : kMeshtasticPresets) {
            const bool sameModem = host.spreadingFactor == preset.spreadingFactor &&
                                   host.bandwidthKhz == preset.bandwidthKhz;
            const bool aligned =
                selectSwitchMode(meshtasticOf(host, preset.frequencyMhz), meshcore) == SwitchMode::Aligned;
            EXPECT_EQ(sameModem, aligned) << host.name;
        }
    }
}

TEST(PresetMatrix, TheCodingRateNeverBlocksAlignment)
{
    for (const Preset &preset : kMeshcorePresets) {
        LoraProfile meshcore = meshcoreOf(preset);
        LoraProfile host = meshtasticOf(preset, preset.frequencyMhz);
        host.codingRate = static_cast<uint8_t>(meshcore.codingRate == 5 ? 8 : 5);

        SCOPED_TRACE(preset.name);
        EXPECT_EQ(SwitchMode::Aligned, selectSwitchMode(host, meshcore));
    }
}

TEST(PresetMatrix, EveryPairingSelectsADefiniteModeAndSchedule)
{
    for (const Preset &preset : kMeshcorePresets) {
        const LoraProfile meshcore = meshcoreOf(preset);

        for (const Preset &host : kMeshtasticPresets) {
            const LoraProfile hostProfile = meshtasticOf(host, preset.frequencyMhz);
            SCOPED_TRACE(std::string(preset.name) + " vs " + host.name);

            FakeRadio radio;
            SliceConfig config;
            SliceScheduler scheduler(radio, config);
            scheduler.setProfiles(hostProfile, meshcore);

            const uint32_t hostFloor = minimumDetectDwellMs(hostProfile);

            EXPECT_GT(scheduler.holdMs(), 0u);
            EXPECT_GT(scheduler.dwellMs(), 0u);
            EXPECT_GE(scheduler.maxDwellMs(), scheduler.dwellMs());
            EXPECT_GE(scheduler.holdMs(), hostFloor);

            if (static_cast<float>(hostFloor) < preambleTimeMs(meshcore))
                EXPECT_LT(static_cast<float>(scheduler.holdMs()), preambleTimeMs(meshcore));
        }
    }
}

TEST(PresetMatrix, SixFastMeshtasticPresetsCannotAbsorbASplitScanExcursion)
{
    const LoraProfile meshcore = meshcoreCardputerProfile();
    const std::string tooFast[] = {"ShortTurbo",  "ShortFast",   "ShortSlow",
                                   "MediumFast",  "MediumTurbo", "NarrowFast"};

    for (const Preset &host : kMeshtasticPresets) {
        const LoraProfile hostProfile = meshtasticOf(host, meshcore.frequencyMhz);
        bool expectedTooFast = false;
        for (const std::string &name : tooFast)
            if (name == host.name)
                expectedTooFast = true;

        SCOPED_TRACE(host.name);
        EXPECT_EQ(!expectedTooFast, hostToleratesScanning(hostProfile, meshcore, SwitchMode::Split));
    }
}

TEST(PresetMatrix, SwitchingTheMeshcoreProfileReDerivesTheHold)
{
    FakeRadio radio;
    SliceConfig config;
    SliceScheduler scheduler(radio, config);

    LoraProfile fast = meshcoreCardputerProfile();
    fast.spreadingFactor = 7;
    fast.bandwidthKhz = 62.5f;
    fast.preambleSymbols = meshcorePreambleSymbols(7);

    scheduler.setProfiles(meshtasticLongFastProfile(), meshcoreDefaultProfile());
    const uint32_t slowHold = scheduler.holdMs();

    scheduler.setProfiles(meshtasticLongFastProfile(), fast);

    EXPECT_NE(slowHold, scheduler.holdMs());
    EXPECT_LT(scheduler.holdMs(), slowHold);
    EXPECT_GE(scheduler.holdMs(), minimumDetectDwellMs(meshtasticLongFastProfile()));
    EXPECT_LT(static_cast<float>(scheduler.holdMs()), preambleTimeMs(fast));
}

TEST(PresetMatrix, AlignedSwitchingRescuesTheFasterPresets)
{
    LoraProfile meshcore = meshcoreCardputerProfile();
    meshcore.spreadingFactor = 7;
    meshcore.bandwidthKhz = 250.0f;
    meshcore.preambleSymbols = meshcorePreambleSymbols(7);

    LoraProfile host = meshcore;
    host.syncWord = 0x2b;
    host.preambleSymbols = 16;

    ASSERT_EQ(SwitchMode::Aligned, selectSwitchMode(host, meshcore));
    EXPECT_FALSE(hostToleratesScanning(host, meshcore, SwitchMode::Split));
    EXPECT_TRUE(hostToleratesScanning(host, meshcore, SwitchMode::Aligned));
}

TEST(PresetMatrix, TheExcursionIsCheaperWhenAligned)
{
    const LoraProfile meshcore = meshcoreCardputerProfile();
    EXPECT_LT(scanExcursionMs(meshcore, SwitchMode::Aligned), scanExcursionMs(meshcore, SwitchMode::Split));
}

TEST(PresetMatrix, TheMatrixCoversEveryPublishedPreset)
{
    EXPECT_EQ(14u, kMeshcoreCount);
    EXPECT_EQ(16u, kMeshtasticCount);
}
