#include <gtest/gtest.h>

#include <string>

#include "meshcompromise/lora_profile.h"
#include "meshcompromise/settings.h"
#include "meshcompromise/ui_text.h"

using namespace meshcompromise;

namespace
{

LoraProfile meshtasticLongFast()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.525f;
    profile.spreadingFactor = 11;
    profile.bandwidthKhz = 250.0f;
    profile.codingRate = 5;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

LoraProfile meshtasticNarrowSlow()
{
    LoraProfile profile = meshtasticLongFast();
    profile.spreadingFactor = 8;
    profile.bandwidthKhz = 62.5f;
    profile.codingRate = 6;
    profile.frequencyMhz = meshcoreDefaultProfile().frequencyMhz;
    return profile;
}

// Matches defaultSettings().meshcore (the real MeshCore Public channel PHY)
// exactly except for sync word - the "nothing to align" case for that
// shipped default, the way meshtasticLongFast() used to be before the
// default moved off Meshtastic's own LongFast parameters.
LoraProfile meshtasticMatchingShippedDefault()
{
    LoraProfile profile;
    profile.frequencyMhz = 869.618f;
    profile.spreadingFactor = 7;
    profile.bandwidthKhz = 62.5f;
    profile.codingRate = 5;
    profile.syncWord = 0x2b;
    profile.preambleSymbols = 16;
    return profile;
}

std::string hintFor(const LoraProfile &meshtastic)
{
    BridgeSettings settings = defaultSettings();
    UiStatus status;
    status.running = true;
    status.blocker = alignmentBlocker(meshtastic, settings.meshcore);
    status.mode = selectSwitchMode(meshtastic, settings.meshcore);

    char hint[kUiLineLength] = {0};
    buildAlignmentHint(status, settings, hint, sizeof(hint));
    return std::string(hint);
}

} // namespace

TEST(Alignment, StockLongFastNowNeedsSplitMode)
{
    // The shipped default targets real MeshCore's Public-channel PHY
    // (SF7/62.5kHz), not Meshtastic's own LongFast (SF11/250kHz) - those are
    // now genuinely different radio configs, so LongFast can no longer share
    // the modem for free the way it could against the old Cardputer-aligned
    // default.
    const LoraProfile shipped = defaultSettings().meshcore;
    EXPECT_EQ(AlignmentBlocker::SpreadingFactor, alignmentBlocker(meshtasticLongFast(), shipped));
    EXPECT_EQ(SwitchMode::Split, selectSwitchMode(meshtasticLongFast(), shipped));
}

TEST(Alignment, AProfileMatchingTheShippedDefaultNeedsNoChangesAtAll)
{
    const LoraProfile shipped = defaultSettings().meshcore;
    EXPECT_EQ(AlignmentBlocker::None, alignmentBlocker(meshtasticMatchingShippedDefault(), shipped));
    EXPECT_EQ("sync-word switch only", hintFor(meshtasticMatchingShippedDefault()));
}

TEST(Alignment, ADifferentPresetIsBlockedByTheSpreadingFactorFirst)
{
    LoraProfile mediumFast = meshtasticMatchingShippedDefault();
    mediumFast.spreadingFactor = 9;

    EXPECT_EQ(AlignmentBlocker::SpreadingFactor, alignmentBlocker(mediumFast, defaultSettings().meshcore));
    EXPECT_EQ("align: Mtastic SF7", hintFor(mediumFast));
}

TEST(Alignment, MatchingSpreadingFactorExposesTheBandwidth)
{
    LoraProfile profile = meshtasticMatchingShippedDefault();
    profile.bandwidthKhz = 125.0f;

    EXPECT_EQ(AlignmentBlocker::Bandwidth, alignmentBlocker(profile, defaultSettings().meshcore));
    EXPECT_EQ("align: Mtastic BW62.5", hintFor(profile));
}

TEST(Alignment, MatchingModemExposesTheFrequency)
{
    LoraProfile profile = meshtasticMatchingShippedDefault();
    profile.frequencyMhz = 868.0f;

    EXPECT_EQ(AlignmentBlocker::Frequency, alignmentBlocker(profile, defaultSettings().meshcore));
    EXPECT_EQ("align: Mtastic 869.618", hintFor(profile));
}

TEST(Alignment, NarrowSlowStillAlignsAgainstTheUpstreamMeshcorePhy)
{
    EXPECT_EQ(AlignmentBlocker::None, alignmentBlocker(meshtasticNarrowSlow(), meshcoreDefaultProfile()));
    EXPECT_EQ(SwitchMode::Aligned, selectSwitchMode(meshtasticNarrowSlow(), meshcoreDefaultProfile()));
}

TEST(Alignment, AnUnconfiguredHostRadioSaysSo)
{
    LoraProfile empty;
    EXPECT_EQ(AlignmentBlocker::Unconfigured, alignmentBlocker(empty, meshcoreDefaultProfile()));
    EXPECT_EQ("no Mtastic radio yet", hintFor(empty));
}

TEST(Alignment, TheBlockerAgreesWithTheSwitchModeForEveryCase)
{
    const LoraProfile profiles[] = {meshtasticLongFast(), meshtasticNarrowSlow(), LoraProfile()};

    for (const LoraProfile &profile : profiles) {
        const bool aligned = alignmentBlocker(profile, meshcoreDefaultProfile()) == AlignmentBlocker::None;
        EXPECT_EQ(aligned, selectSwitchMode(profile, meshcoreDefaultProfile()) == SwitchMode::Aligned);
    }
}

TEST(Alignment, EveryHintFitsOnTheDisplay)
{
    const LoraProfile profiles[] = {meshtasticLongFast(), meshtasticNarrowSlow(), LoraProfile()};

    for (const LoraProfile &profile : profiles)
        EXPECT_LT(hintFor(profile).size(), kUiLineLength);

    LoraProfile bandwidth = meshtasticLongFast();
    bandwidth.bandwidthKhz = 125.0f;
    EXPECT_LT(hintFor(bandwidth).size(), kUiLineLength);
}

TEST(Alignment, TheStatusFrameCarriesTheModeMarkAndTheHint)
{
    BridgeSettings settings = defaultSettings();
    UiStatus status;
    status.running = true;
    LoraProfile mismatched = meshtasticLongFast();
    mismatched.spreadingFactor = 9;
    status.blocker = alignmentBlocker(mismatched, settings.meshcore);
    status.mode = SwitchMode::Split;

    UiLine lines[kUiMaxLines];
    const size_t count = buildStatusLines(settings, status, lines, kUiMaxLines);

    ASSERT_GE(count, 3u);
    EXPECT_NE(std::string::npos, std::string(lines[1].text).find("[SPLIT]"));
    EXPECT_EQ("align: Mtastic SF7", std::string(lines[2].text));
}

TEST(Alignment, AnAlignedStatusFrameMarksItClearly)
{
    BridgeSettings settings = defaultSettings();
    UiStatus status;
    status.running = true;
    status.blocker = AlignmentBlocker::None;
    status.mode = SwitchMode::Aligned;

    UiLine lines[kUiMaxLines];
    const size_t count = buildStatusLines(settings, status, lines, kUiMaxLines);

    ASSERT_GE(count, 3u);
    EXPECT_NE(std::string::npos, std::string(lines[1].text).find("[ALIGNED]"));
    EXPECT_EQ("sync-word switch only", std::string(lines[2].text));
}

TEST(Alignment, ADisabledBridgeShowsNoHint)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcoreEnabled = false;

    UiStatus status;
    status.blocker = AlignmentBlocker::SpreadingFactor;

    UiLine lines[kUiMaxLines];
    buildStatusLines(settings, status, lines, kUiMaxLines);

    EXPECT_EQ(std::string::npos, std::string(lines[2].text).find("align:"));
}

TEST(Alignment, TheHintRefusesAZeroSizedBuffer)
{
    BridgeSettings settings = defaultSettings();
    UiStatus status;
    status.blocker = AlignmentBlocker::None;

    char scratch[1] = {0};
    EXPECT_EQ(0u, buildAlignmentHint(status, settings, nullptr, sizeof(scratch)));
    EXPECT_EQ(0u, buildAlignmentHint(status, settings, scratch, 0));
}

TEST(Alignment, AnOverlyFastHostPresetOutranksTheAlignmentHint)
{
    BridgeSettings settings = defaultSettings();
    UiStatus status;
    status.running = true;
    status.blocker = AlignmentBlocker::SpreadingFactor;
    status.hostToleratesScanning = false;

    char hint[kUiLineLength] = {0};
    ASSERT_GT(buildAlignmentHint(status, settings, hint, sizeof(hint)), 0u);
    EXPECT_EQ("Mtastic preset too fast", std::string(hint));
    EXPECT_LT(std::string(hint).size(), kUiLineLength);
}
