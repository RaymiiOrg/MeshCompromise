#include <cstdio>

#include "meshcompromise/ui_text.h"

using namespace meshcompromise;

namespace
{

constexpr size_t kBodyRows = 6;

void emit(const char *name, const UiLine *lines, size_t count, size_t first)
{
    printf("%s\n", name);
    printf(" \t%s\n", lines[0].text);

    size_t row = 0;
    for (size_t index = first; index < count && row < kBodyRows; index++, row++)
        printf("%c\t%s\n", lines[index].selected ? '>' : ' ', lines[index].text);

    printf("\n");
}

} // namespace

int main()
{
    BridgeSettings settings = defaultSettings();

    UiStatus status;
    status.running = true;
    status.mode = SwitchMode::Aligned;
    status.packetsHeard = 42;
    status.packetsSent = 9;
    status.mirrored = 17;
    status.meshcoreDutyCycle = 0.22f;
    status.blocker = AlignmentBlocker::None;

    UiLine lines[kUiMaxLines];

    size_t count = buildStatusLines(settings, status, lines, kUiMaxLines);
    emit("status", lines, count, 1);

    UiStatus split = status;
    split.mode = SwitchMode::Split;
    split.blocker = AlignmentBlocker::SpreadingFactor;
    count = buildStatusLines(settings, split, lines, kUiMaxLines);
    emit("status-split", lines, count, 1);

    const uint8_t cursor = static_cast<uint8_t>(SettingField::SpreadingFactor);
    count = buildSettingsLines(settings, cursor, false, lines, kUiMaxLines);
    emit("settings", lines, count, visibleWindowStart(cursor, kBodyRows));

    const uint8_t scrolled = static_cast<uint8_t>(SettingField::AdvertInterval);
    count = buildSettingsLines(settings, scrolled, true, lines, kUiMaxLines);
    emit("settings-scrolled", lines, count, visibleWindowStart(scrolled, kBodyRows));

    return 0;
}
