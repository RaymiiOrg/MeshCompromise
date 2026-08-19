#include <gtest/gtest.h>

#include <string>

#include "meshcompromise/ui_text.h"

using namespace meshcompromise;

namespace
{

UiStatus runningStatus()
{
    UiStatus status;
    status.running = true;
    status.mode = SwitchMode::Aligned;
    status.packetsHeard = 12;
    status.packetsSent = 3;
    status.mirrored = 7;
    status.injected = 2;
    status.adverts = 4;
    status.meshcoreDutyCycle = 0.18f;
    status.blocker = AlignmentBlocker::None;
    return status;
}

std::string lineAt(const UiLine *lines, size_t index)
{
    return std::string(lines[index].text);
}

} // namespace

TEST(UiText, StatusHeaderNamesTheProtocol)
{
    UiLine lines[kUiMaxLines];
    const size_t count = buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);

    ASSERT_GT(count, 0u);
    EXPECT_EQ(lineAt(lines, 0), "MeshCore");
}

TEST(UiText, StatusShowsListeningAndAlignedMode)
{
    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 1), "listening  [ALIGNED]");
}

TEST(UiText, StatusShowsSplitMode)
{
    UiStatus status = runningStatus();
    status.mode = SwitchMode::Split;

    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), status, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 1), "listening  [SPLIT]");
}

TEST(UiText, StatusShowsOffWhenMeshcoreDisabled)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcoreEnabled = false;

    UiLine lines[kUiMaxLines];
    buildStatusLines(settings, runningStatus(), lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 1), "off  [ALIGNED]");
}

TEST(UiText, StatusShowsStartingBeforeFirstTick)
{
    UiStatus status = runningStatus();
    status.running = false;

    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), status, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 1), "starting  [ALIGNED]");
}

TEST(UiText, StatusShowsRadioParameters)
{
    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 3), "869.618 SF7 BW62.5");
}

TEST(UiText, StatusShowsCountersAndRoundedDutyCycle)
{
    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 4), "rx 12  tx 3  18%");
}

TEST(UiText, StatusShowsBothMirrorDirectionsAndAdverts)
{
    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 5), "mirror on 7/2 adv 4");
}

TEST(UiText, StatusShowsHeapWhenNothingHasBeenHeard)
{
    UiStatus status = runningStatus();
    status.freeHeapBytes = 143360;

    UiLine lines[kUiMaxLines];
    const size_t count = buildStatusLines(defaultSettings(), status, lines, kUiMaxLines);
    ASSERT_EQ(count, 7u);
    EXPECT_EQ(lineAt(lines, 6), "heap 140k");
}

TEST(UiText, StatusPrefersTheLastReceivedTextOverHeap)
{
    UiStatus status = runningStatus();
    status.freeHeapBytes = 143360;
    status.lastText = "bob: hi there";

    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), status, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 6), "bob: hi there");
}

TEST(UiText, StatusOmitsTheLastLineWhenThereIsNothingToShow)
{
    UiLine lines[kUiMaxLines];
    EXPECT_EQ(buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines), 6u);
}

TEST(UiText, StatusIgnoresAnEmptyReceivedText)
{
    UiStatus status = runningStatus();
    status.lastText = "";
    status.freeHeapBytes = 65536;

    UiLine lines[kUiMaxLines];
    buildStatusLines(defaultSettings(), status, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 6), "heap 64k");
}

TEST(UiText, StatusRespectsTheLineBudget)
{
    UiLine lines[2];
    EXPECT_EQ(buildStatusLines(defaultSettings(), runningStatus(), lines, 2), 2u);
    EXPECT_EQ(buildStatusLines(defaultSettings(), runningStatus(), lines, 0), 0u);
}

TEST(UiText, SettingsHeaderMarksEditing)
{
    UiLine lines[kUiMaxLines];
    buildSettingsLines(defaultSettings(), 0, false, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 0), "Settings");

    buildSettingsLines(defaultSettings(), 0, true, lines, kUiMaxLines);
    EXPECT_EQ(lineAt(lines, 0), "Settings *");
}

TEST(UiText, SettingsMarksTheCursorRow)
{
    UiLine lines[kUiMaxLines];
    buildSettingsLines(defaultSettings(), 0, false, lines, kUiMaxLines);

    EXPECT_EQ(lineAt(lines, 1), "MeshCore on");
    EXPECT_TRUE(lines[1].selected);
    EXPECT_FALSE(lines[2].selected);
}

TEST(UiText, SettingsCursorMovesToTheNamedField)
{
    UiLine lines[kUiMaxLines];
    buildSettingsLines(defaultSettings(), static_cast<uint8_t>(SettingField::SpreadingFactor), false, lines, kUiMaxLines);

    EXPECT_EQ(lineAt(lines, 1 + static_cast<size_t>(SettingField::SpreadingFactor)), "SF 7");
}

TEST(UiText, SettingsRendersEveryFieldWithUnits)
{
    UiLine lines[kUiMaxLines];
    const size_t count = buildSettingsLines(defaultSettings(), 0, false, lines, kUiMaxLines);

    ASSERT_EQ(count, 1u + static_cast<size_t>(SettingField::Count));
    EXPECT_EQ(lineAt(lines, 2), "Freq 869.618 MHz");
    EXPECT_EQ(lineAt(lines, 3), "BW 62.5 kHz");
    EXPECT_EQ(lineAt(lines, 5), "CR 4/5");
    EXPECT_EQ(lineAt(lines, 6), "Power 22 dBm");
    EXPECT_EQ(lineAt(lines, 9), "Mirror on");
    EXPECT_EQ(lineAt(lines, 10), "Reverse on");
    EXPECT_EQ(lineAt(lines, 11), "Source all");
    EXPECT_EQ(lineAt(lines, 12), "Advert 60 min");
    EXPECT_EQ(lineAt(lines, 14), "Dwell auto");
    EXPECT_EQ(lineAt(lines, 15), "Hold auto");
}

TEST(UiText, DerivedTimingsReadAsAuto)
{
    const BridgeSettings settings = defaultSettings();
    ASSERT_EQ(settings.slice.scanDwellMs, 0u);
    ASSERT_EQ(settings.slice.meshtasticHoldMs, 0u);

    char value[kUiLineLength] = {0};
    settingValue(settings, SettingField::MeshcoreDwellMs, value, sizeof(value));
    EXPECT_STREQ(value, "auto");
    settingValue(settings, SettingField::MeshtasticHoldMs, value, sizeof(value));
    EXPECT_STREQ(value, "auto");
}

TEST(UiText, OverriddenTimingsReadInMilliseconds)
{
    BridgeSettings settings = defaultSettings();
    settings.slice.scanDwellMs = 120;
    settings.slice.meshtasticHoldMs = 90;

    char value[kUiLineLength] = {0};
    settingValue(settings, SettingField::MeshcoreDwellMs, value, sizeof(value));
    EXPECT_STREQ(value, "120 ms");
    settingValue(settings, SettingField::MeshtasticHoldMs, value, sizeof(value));
    EXPECT_STREQ(value, "90 ms");
}

TEST(UiText, AdjustStepsTheTimingsInTenMillisecondJumps)
{
    BridgeSettings settings = defaultSettings();

    adjustSetting(settings, SettingField::MeshcoreDwellMs, 1);
    EXPECT_EQ(settings.slice.scanDwellMs, 10u);

    adjustSetting(settings, SettingField::MeshtasticHoldMs, 1);
    EXPECT_EQ(settings.slice.meshtasticHoldMs, 10u);
}

TEST(UiText, AdjustReturnsTheTimingsToAuto)
{
    BridgeSettings settings = defaultSettings();
    settings.slice.scanDwellMs = 10;
    settings.slice.meshtasticHoldMs = 10;

    adjustSetting(settings, SettingField::MeshcoreDwellMs, -1);
    adjustSetting(settings, SettingField::MeshtasticHoldMs, -1);

    EXPECT_EQ(settings.slice.scanDwellMs, 0u);
    EXPECT_EQ(settings.slice.meshtasticHoldMs, 0u);
}

TEST(UiText, AdjustClampsTheTimings)
{
    BridgeSettings settings = defaultSettings();

    for (int i = 0; i < 400; i++) {
        adjustSetting(settings, SettingField::MeshcoreDwellMs, 1);
        adjustSetting(settings, SettingField::MeshtasticHoldMs, 1);
    }
    EXPECT_EQ(settings.slice.scanDwellMs, 2000u);
    EXPECT_EQ(settings.slice.meshtasticHoldMs, 2000u);

    for (int i = 0; i < 400; i++) {
        adjustSetting(settings, SettingField::MeshcoreDwellMs, -1);
        adjustSetting(settings, SettingField::MeshtasticHoldMs, -1);
    }
    EXPECT_EQ(settings.slice.scanDwellMs, 0u);
    EXPECT_EQ(settings.slice.meshtasticHoldMs, 0u);
}

TEST(UiText, SettingsFitTheLineBuffer)
{
    EXPECT_LE(1u + static_cast<size_t>(SettingField::Count), kUiMaxLines);
}

TEST(UiText, AdjustTogglesReverseMirroring)
{
    BridgeSettings settings = defaultSettings();
    ASSERT_TRUE(settings.mirror.reverseEnabled);

    adjustSetting(settings, SettingField::ReverseMirroring, 1);
    EXPECT_FALSE(settings.mirror.reverseEnabled);

    adjustSetting(settings, SettingField::ReverseMirroring, -1);
    EXPECT_TRUE(settings.mirror.reverseEnabled);
}

TEST(UiText, AdjustStepsAdvertIntervalInSingleMinutes)
{
    BridgeSettings settings = defaultSettings();
    adjustSetting(settings, SettingField::AdvertInterval, 1);
    EXPECT_EQ(settings.advertIntervalMinutes, 61);

    adjustSetting(settings, SettingField::AdvertInterval, -1);
    EXPECT_EQ(settings.advertIntervalMinutes, 60);
}

TEST(UiText, AdjustReachesEveryAdvertIntervalMinute)
{
    BridgeSettings settings = defaultSettings();
    for (int i = 0; i < 59; i++)
        adjustSetting(settings, SettingField::AdvertInterval, -1);
    EXPECT_EQ(settings.advertIntervalMinutes, 1);
}

TEST(UiText, AdjustClampsAdvertInterval)
{
    BridgeSettings settings = defaultSettings();
    for (int i = 0; i < 300; i++)
        adjustSetting(settings, SettingField::AdvertInterval, 1);
    EXPECT_EQ(settings.advertIntervalMinutes, 240);

    for (int i = 0; i < 300; i++)
        adjustSetting(settings, SettingField::AdvertInterval, -1);
    EXPECT_EQ(settings.advertIntervalMinutes, 0);
}

TEST(UiText, DisabledAdvertIntervalReadsAsOff)
{
    BridgeSettings settings = defaultSettings();
    settings.advertIntervalMinutes = 0;

    char value[kUiLineLength] = {0};
    settingValue(settings, SettingField::AdvertInterval, value, sizeof(value));
    EXPECT_STREQ(value, "off");
}

TEST(UiText, EveryFieldHasALabel)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(SettingField::Count); i++)
        EXPECT_STRNE(settingLabel(static_cast<SettingField>(i)), "");
}

TEST(UiText, LinesNeverOverflowTheBuffer)
{
    BridgeSettings settings = defaultSettings();
    settings.meshcore.frequencyMhz = 2499.999f;

    UiLine lines[kUiMaxLines];
    const size_t count = buildSettingsLines(settings, 0, true, lines, kUiMaxLines);
    for (size_t i = 0; i < count; i++)
        EXPECT_LT(std::string(lines[i].text).size(), kUiLineLength);
}

TEST(UiText, TitlesStayShortEnoughForTheHeaderBar)
{
    UiLine lines[kUiMaxLines];

    buildStatusLines(defaultSettings(), runningStatus(), lines, kUiMaxLines);
    EXPECT_LE(lineAt(lines, 0).size(), 12u);

    buildSettingsLines(defaultSettings(), 0, true, lines, kUiMaxLines);
    EXPECT_LE(lineAt(lines, 0).size(), 12u);
}

TEST(UiText, WindowStaysAtTheTopUntilTheCursorLeavesIt)
{
    EXPECT_EQ(visibleWindowStart(0, 6), 1u);
    EXPECT_EQ(visibleWindowStart(4, 6), 1u);
}

TEST(UiText, WindowScrollsToKeepTheCursorOnTheLastRow)
{
    EXPECT_EQ(visibleWindowStart(5, 6), 1u);
    EXPECT_EQ(visibleWindowStart(6, 6), 2u);
    EXPECT_EQ(visibleWindowStart(9, 6), 5u);
}

TEST(UiText, WindowKeepsEveryCursorPositionVisible)
{
    const size_t rows = 6;
    for (uint8_t cursor = 0; cursor < static_cast<uint8_t>(SettingField::Count); cursor++) {
        const size_t first = visibleWindowStart(cursor, rows);
        const size_t cursorLine = static_cast<size_t>(cursor) + 1;
        EXPECT_GE(cursorLine, first);
        EXPECT_LT(cursorLine, first + rows);
    }
}

TEST(UiText, WindowIsSafeWithNoRows)
{
    EXPECT_EQ(visibleWindowStart(3, 0), 1u);
}

TEST(UiText, AdjustTogglesBooleans)
{
    BridgeSettings settings = defaultSettings();

    adjustSetting(settings, SettingField::Mirroring, 1);
    EXPECT_FALSE(settings.mirror.enabled);

    ASSERT_EQ(settings.mirror.policy, MirrorPolicy::AllBroadcasts);
    adjustSetting(settings, SettingField::MirrorSource, 1);
    EXPECT_EQ(settings.mirror.policy, MirrorPolicy::LocalOnly);
}

TEST(UiText, AdjustClampsSpreadingFactor)
{
    BridgeSettings settings = defaultSettings();
    for (int i = 0; i < 20; i++)
        adjustSetting(settings, SettingField::SpreadingFactor, 1);
    EXPECT_EQ(settings.meshcore.spreadingFactor, 12);

    for (int i = 0; i < 20; i++)
        adjustSetting(settings, SettingField::SpreadingFactor, -1);
    EXPECT_EQ(settings.meshcore.spreadingFactor, 5);
}

TEST(UiText, AdjustWalksTheDiscreteBandwidthLadder)
{
    BridgeSettings settings = defaultSettings();
    ASSERT_FLOAT_EQ(settings.meshcore.bandwidthKhz, 62.5f);

    adjustSetting(settings, SettingField::Bandwidth, -1);
    EXPECT_FLOAT_EQ(settings.meshcore.bandwidthKhz, 41.7f);

    adjustSetting(settings, SettingField::Bandwidth, 1);
    EXPECT_FLOAT_EQ(settings.meshcore.bandwidthKhz, 62.5f);
}

TEST(UiText, AdjustKeepsPreambleConsistentWithSpreadingFactor)
{
    BridgeSettings settings = defaultSettings();
    adjustSetting(settings, SettingField::SpreadingFactor, 3);
    EXPECT_EQ(settings.meshcore.spreadingFactor, 10);
    EXPECT_EQ(settings.meshcore.preambleSymbols, 16);
}

TEST(UiText, AdjustLeavesSettingsValid)
{
    BridgeSettings settings = defaultSettings();
    for (uint8_t i = 0; i < static_cast<uint8_t>(SettingField::Count); i++) {
        for (int step = 0; step < 30; step++)
            adjustSetting(settings, static_cast<SettingField>(i), 1);
        for (int step = 0; step < 30; step++)
            adjustSetting(settings, static_cast<SettingField>(i), -1);
    }
    EXPECT_TRUE(validateSettings(settings));
}
