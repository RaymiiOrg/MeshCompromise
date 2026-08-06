#include "meshcompromise/ui_text.h"

#include <cmath>

#include <cstdarg>
#include <cstdio>

namespace meshcompromise
{

namespace
{

constexpr float kBandwidthSteps[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
constexpr int kBandwidthStepCount = static_cast<int>(sizeof(kBandwidthSteps) / sizeof(kBandwidthSteps[0]));

int clampInt(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void writeLine(UiLine *line, bool selected, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(line->text, kUiLineLength, format, args);
    va_end(args);
    line->selected = selected;
}

} // namespace

const char *settingLabel(SettingField field)
{
    switch (field) {
    case SettingField::Enabled:
        return "MeshCore";
    case SettingField::Frequency:
        return "Freq";
    case SettingField::Bandwidth:
        return "BW";
    case SettingField::SpreadingFactor:
        return "SF";
    case SettingField::CodingRate:
        return "CR";
    case SettingField::TxPower:
        return "Power";
    case SettingField::HopLimit:
        return "Hops";
    case SettingField::MeshcoreChannel:
        return "Channel";
    case SettingField::Mirroring:
        return "Mirror";
    case SettingField::ReverseMirroring:
        return "Reverse";
    case SettingField::MirrorSource:
        return "Source";
    case SettingField::AdvertInterval:
        return "Advert";
    case SettingField::StatsInterval:
        return "Stats";
    case SettingField::MeshcoreDwellMs:
        return "Dwell";
    case SettingField::MeshtasticHoldMs:
        return "Hold";
    default:
        return "";
    }
}

void settingValue(const BridgeSettings &settings, SettingField field, char *out, size_t length)
{
    if (out == nullptr || length == 0)
        return;

    switch (field) {
    case SettingField::Enabled:
        snprintf(out, length, "%s", settings.meshcoreEnabled ? "on" : "off");
        break;
    case SettingField::Frequency:
        snprintf(out, length, "%.3f MHz", static_cast<double>(settings.meshcore.frequencyMhz));
        break;
    case SettingField::Bandwidth:
        snprintf(out, length, "%.1f kHz", static_cast<double>(settings.meshcore.bandwidthKhz));
        break;
    case SettingField::SpreadingFactor:
        snprintf(out, length, "%u", static_cast<unsigned>(settings.meshcore.spreadingFactor));
        break;
    case SettingField::CodingRate:
        snprintf(out, length, "4/%u", static_cast<unsigned>(settings.meshcore.codingRate));
        break;
    case SettingField::TxPower:
        snprintf(out, length, "%d dBm", static_cast<int>(settings.txPowerDbm));
        break;
    case SettingField::HopLimit:
        snprintf(out, length, "%u", static_cast<unsigned>(settings.hopLimit));
        break;
    case SettingField::MeshcoreChannel:
        snprintf(out, length, "%u", static_cast<unsigned>(settings.mirror.meshcoreChannel));
        break;
    case SettingField::Mirroring:
        snprintf(out, length, "%s", settings.mirror.enabled ? "on" : "off");
        break;
    case SettingField::ReverseMirroring:
        snprintf(out, length, "%s", settings.mirror.reverseEnabled ? "on" : "off");
        break;
    case SettingField::AdvertInterval:
        if (settings.advertIntervalMinutes == 0)
            snprintf(out, length, "off");
        else
            snprintf(out, length, "%u min", static_cast<unsigned>(settings.advertIntervalMinutes));
        break;
    case SettingField::StatsInterval:
        if (settings.statsIntervalMinutes == 0)
            snprintf(out, length, "off");
        else
            snprintf(out, length, "%u min", static_cast<unsigned>(settings.statsIntervalMinutes));
        break;
    case SettingField::MirrorSource:
        snprintf(out, length, "%s", settings.mirror.policy == MirrorPolicy::LocalOnly ? "local" : "all");
        break;
    case SettingField::MeshcoreDwellMs:
        if (settings.slice.scanDwellMs == 0)
            snprintf(out, length, "auto");
        else
            snprintf(out, length, "%u ms", static_cast<unsigned>(settings.slice.scanDwellMs));
        break;
    case SettingField::MeshtasticHoldMs:
        if (settings.slice.meshtasticHoldMs == 0)
            snprintf(out, length, "auto");
        else
            snprintf(out, length, "%u ms", static_cast<unsigned>(settings.slice.meshtasticHoldMs));
        break;
    default:
        out[0] = '\0';
        break;
    }
}

size_t buildAlignmentHint(const UiStatus &status, const BridgeSettings &settings, char *out, size_t capacity)
{
    if (out == nullptr || capacity == 0)
        return 0;

    if (!status.hostToleratesScanning) {
        const int fast = snprintf(out, capacity, "Mtastic preset too fast");
        return fast > 0 && static_cast<size_t>(fast) < capacity ? static_cast<size_t>(fast) : capacity - 1;
    }

    int written = 0;
    switch (status.blocker) {
    case AlignmentBlocker::None:
        written = snprintf(out, capacity, "sync-word switch only");
        break;
    case AlignmentBlocker::SpreadingFactor:
        written = snprintf(out, capacity, "align: Mtastic SF%u",
                           static_cast<unsigned>(settings.meshcore.spreadingFactor));
        break;
    case AlignmentBlocker::Bandwidth:
        written = snprintf(out, capacity, "align: Mtastic BW%.1f", static_cast<double>(settings.meshcore.bandwidthKhz));
        break;
    case AlignmentBlocker::Frequency:
        written = snprintf(out, capacity, "align: Mtastic %.3f", static_cast<double>(settings.meshcore.frequencyMhz));
        break;
    default:
        written = snprintf(out, capacity, "no Mtastic radio yet");
        break;
    }

    if (written <= 0)
        return 0;
    return static_cast<size_t>(written) < capacity ? static_cast<size_t>(written) : capacity - 1;
}

size_t buildStatusLines(const BridgeSettings &settings, const UiStatus &status, UiLine *out, size_t max)
{
    if (out == nullptr || max == 0)
        return 0;

    size_t count = 0;

    if (count < max)
        writeLine(&out[count++], false, "MeshCore");

    if (count < max) {
        const char *state = !settings.meshcoreEnabled ? "off" : (status.running ? "listening" : "starting");
        writeLine(&out[count++], false, "%s  %s", state,
                  status.mode == SwitchMode::Aligned ? "[ALIGNED]" : "[SPLIT]");
    }

    if (count < max && settings.meshcoreEnabled) {
        char hint[kUiLineLength] = {0};
        if (buildAlignmentHint(status, settings, hint, sizeof(hint)) > 0)
            writeLine(&out[count++], false, "%s", hint);
    }

    if (count < max)
        writeLine(&out[count++], false, "%.3f SF%u BW%.1f", static_cast<double>(settings.meshcore.frequencyMhz),
                  static_cast<unsigned>(settings.meshcore.spreadingFactor),
                  static_cast<double>(settings.meshcore.bandwidthKhz));

    if (count < max)
        writeLine(&out[count++], false, "rx %lu  tx %lu  %u%%", static_cast<unsigned long>(status.packetsHeard),
                  static_cast<unsigned long>(status.packetsSent),
                  static_cast<unsigned>(std::lround(status.meshcoreDutyCycle * 100.0f)));

    if (count < max)
        writeLine(&out[count++], false, "mirror %s %lu/%lu adv %lu", settings.mirror.enabled ? "on" : "off",
                  static_cast<unsigned long>(status.mirrored), static_cast<unsigned long>(status.injected),
                  static_cast<unsigned long>(status.adverts));

    if (count < max && status.lastText != nullptr && status.lastText[0] != '\0')
        writeLine(&out[count++], false, "%s", status.lastText);
    else if (count < max && status.freeHeapBytes > 0)
        writeLine(&out[count++], false, "heap %luk", static_cast<unsigned long>(status.freeHeapBytes / 1024));

    return count;
}

size_t buildSettingsLines(const BridgeSettings &settings, uint8_t cursor, bool editing, UiLine *out, size_t max)
{
    if (out == nullptr || max == 0)
        return 0;

    size_t count = 0;

    if (count < max)
        writeLine(&out[count++], false, "Settings%s", editing ? " *" : "");

    const uint8_t total = static_cast<uint8_t>(SettingField::Count);
    char value[kUiLineLength] = {0};

    for (uint8_t index = 0; index < total && count < max; index++) {
        const SettingField field = static_cast<SettingField>(index);
        settingValue(settings, field, value, sizeof(value));
        const bool selected = index == cursor;
        writeLine(&out[count++], selected, "%s %s", settingLabel(field), value);
    }

    return count;
}

size_t visibleWindowStart(uint8_t cursor, size_t bodyRows)
{
    if (bodyRows == 0)
        return 1;
    if (static_cast<size_t>(cursor) + 1 < bodyRows)
        return 1;
    return static_cast<size_t>(cursor) + 2 - bodyRows;
}

void adjustSetting(BridgeSettings &settings, SettingField field, int direction)
{
    switch (field) {
    case SettingField::Enabled:
        settings.meshcoreEnabled = !settings.meshcoreEnabled;
        break;
    case SettingField::Frequency:
        settings.meshcore.frequencyMhz += static_cast<float>(direction) * 0.025f;
        break;
    case SettingField::Bandwidth: {
        int index = 0;
        for (int i = 0; i < kBandwidthStepCount; i++) {
            if (kBandwidthSteps[i] >= settings.meshcore.bandwidthKhz - 0.01f) {
                index = i;
                break;
            }
        }
        index = clampInt(index + direction, 0, kBandwidthStepCount - 1);
        settings.meshcore.bandwidthKhz = kBandwidthSteps[index];
        break;
    }
    case SettingField::SpreadingFactor:
        settings.meshcore.spreadingFactor =
            static_cast<uint8_t>(clampInt(static_cast<int>(settings.meshcore.spreadingFactor) + direction, 5, 12));
        break;
    case SettingField::CodingRate:
        settings.meshcore.codingRate =
            static_cast<uint8_t>(clampInt(static_cast<int>(settings.meshcore.codingRate) + direction, 5, 8));
        break;
    case SettingField::TxPower:
        settings.txPowerDbm = static_cast<int8_t>(clampInt(static_cast<int>(settings.txPowerDbm) + direction, -9, 22));
        break;
    case SettingField::HopLimit:
        settings.hopLimit = static_cast<uint8_t>(clampInt(static_cast<int>(settings.hopLimit) + direction, 0, 7));
        break;
    case SettingField::MeshcoreChannel:
        settings.mirror.meshcoreChannel =
            static_cast<uint8_t>(clampInt(static_cast<int>(settings.mirror.meshcoreChannel) + direction, 0, 7));
        break;
    case SettingField::Mirroring:
        settings.mirror.enabled = !settings.mirror.enabled;
        break;
    case SettingField::ReverseMirroring:
        settings.mirror.reverseEnabled = !settings.mirror.reverseEnabled;
        break;
    case SettingField::AdvertInterval:
        settings.advertIntervalMinutes =
            static_cast<uint16_t>(clampInt(static_cast<int>(settings.advertIntervalMinutes) + direction, 0, 240));
        break;
    case SettingField::StatsInterval:
        settings.statsIntervalMinutes =
            static_cast<uint16_t>(clampInt(static_cast<int>(settings.statsIntervalMinutes) + direction, 0, 240));
        break;
    case SettingField::MirrorSource:
        settings.mirror.policy =
            settings.mirror.policy == MirrorPolicy::LocalOnly ? MirrorPolicy::AllBroadcasts : MirrorPolicy::LocalOnly;
        break;
    case SettingField::MeshcoreDwellMs:
        settings.slice.scanDwellMs =
            static_cast<uint32_t>(clampInt(static_cast<int>(settings.slice.scanDwellMs) + direction * 10, 0, 2000));
        break;
    case SettingField::MeshtasticHoldMs:
        settings.slice.meshtasticHoldMs =
            static_cast<uint32_t>(clampInt(static_cast<int>(settings.slice.meshtasticHoldMs) + direction * 10, 0, 2000));
        break;
    default:
        break;
    }

    normalizeSettings(settings);
}

} // namespace meshcompromise
