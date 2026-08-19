#pragma once

#include <cstddef>
#include <cstdint>

#include "meshcompromise/settings.h"
#include "meshcompromise/slice_scheduler.h"

namespace meshcompromise
{

constexpr size_t kUiLineLength = 32;
constexpr size_t kUiMaxLines = 20;

enum class SettingField {
    Enabled,
    Frequency,
    Bandwidth,
    SpreadingFactor,
    CodingRate,
    TxPower,
    HopLimit,
    MeshcoreChannel,
    Mirroring,
    ReverseMirroring,
    MirrorSource,
    AdvertInterval,
    StatsInterval,
    MeshcoreDwellMs,
    MeshtasticHoldMs,
    Count
};

struct UiLine {
    char text[kUiLineLength] = {0};
    bool selected = false;
};

struct UiStatus {
    bool running = false;
    SwitchMode mode = SwitchMode::Split;
    uint32_t packetsHeard = 0;
    uint32_t packetsSent = 0;
    uint32_t mirrored = 0;
    uint32_t injected = 0;
    uint32_t adverts = 0;
    float meshcoreDutyCycle = 0.0f;
    uint32_t freeHeapBytes = 0;
    const char *lastText = nullptr;
    AlignmentBlocker blocker = AlignmentBlocker::Unconfigured;
    bool hostToleratesScanning = true;
};

size_t buildAlignmentHint(const UiStatus &status, const BridgeSettings &settings, char *out, size_t capacity);

const char *settingLabel(SettingField field);

void settingValue(const BridgeSettings &settings, SettingField field, char *out, size_t length);

size_t buildStatusLines(const BridgeSettings &settings, const UiStatus &status, UiLine *out, size_t max);

size_t buildSettingsLines(const BridgeSettings &settings, uint8_t cursor, bool editing, UiLine *out, size_t max);

void adjustSetting(BridgeSettings &settings, SettingField field, int direction);

size_t visibleWindowStart(uint8_t cursor, size_t bodyRows);

} // namespace meshcompromise
