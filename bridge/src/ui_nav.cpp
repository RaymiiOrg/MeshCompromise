#include "meshcompromise/ui_nav.h"

namespace meshcompromise
{

namespace
{

UiNavResult handledRedraw()
{
    UiNavResult result;
    result.handled = true;
    result.redraw = true;
    return result;
}

constexpr uint32_t kSelectDoubleTapWindowMs = 500;

UiNavResult enterSettings(UiNavState &state)
{
    state.page = UiPage::Settings;
    state.cursor = 0;
    state.editing = false;
    state.lastSelectMs = 0;
    UiNavResult result = handledRedraw();
    result.reloadDraft = true;
    return result;
}

} // namespace

UiNavResult navigate(UiNavState &state, UiKey key, uint32_t nowMs)
{
    if (key == UiKey::Cancel) {
        if (state.editing) {
            state.editing = false;
            UiNavResult result = handledRedraw();
            result.commit = true;
            return result;
        }
        if (state.page == UiPage::Settings) {
            state.page = UiPage::Status;
            return handledRedraw();
        }
        return UiNavResult();
    }

    if (key == UiKey::SelectLong) {
        if (state.page == UiPage::Status)
            return enterSettings(state);
        state.page = UiPage::Status;
        state.cursor = 0;
        state.editing = false;
        UiNavResult result = handledRedraw();
        result.reloadDraft = true;
        return result;
    }

    // Button/rotary/trackball input can hold long enough to raise
    // UiKey::SelectLong above, but keyboard-matrix drivers (Cardputer, Q10,
    // MPR121, T-Deck...) never do - TCA8418KeyboardBase and friends only ever
    // queue a plain Select on release, with no press-duration tracking at
    // all. Without this fallback the Settings page is unreachable on any
    // keyboard-equipped board: a lone Select does nothing while on Status,
    // and SelectLong simply never arrives.
    if (key == UiKey::Select && state.page == UiPage::Status) {
        const bool doubleTapped = state.lastSelectMs != 0 && nowMs - state.lastSelectMs <= kSelectDoubleTapWindowMs;
        if (doubleTapped)
            return enterSettings(state);
        state.lastSelectMs = nowMs;
        return UiNavResult();
    }

    if (state.page != UiPage::Settings)
        return UiNavResult();

    switch (key) {
    case UiKey::Select: {
        state.editing = !state.editing;
        UiNavResult result = handledRedraw();
        result.commit = !state.editing;
        return result;
    }
    case UiKey::Up: {
        UiNavResult result = handledRedraw();
        if (state.editing)
            result.adjust = 1;
        else if (state.cursor > 0)
            state.cursor--;
        return result;
    }
    case UiKey::Down: {
        UiNavResult result = handledRedraw();
        if (state.editing)
            result.adjust = -1;
        else if (state.cursor + 1 < static_cast<uint8_t>(SettingField::Count))
            state.cursor++;
        return result;
    }
    case UiKey::Left: {
        UiNavResult result = handledRedraw();
        result.adjust = -1;
        return result;
    }
    case UiKey::Right: {
        UiNavResult result = handledRedraw();
        result.adjust = 1;
        return result;
    }
    default:
        return UiNavResult();
    }
}

size_t planRows(const UiLine *lines, size_t count, size_t first, size_t bodyRows, UiRowPlan *out, size_t max)
{
    if (lines == nullptr || out == nullptr)
        return 0;

    size_t written = 0;
    uint8_t row = 1;

    for (size_t index = first; index < count && row <= bodyRows && written < max; index++, row++) {
        out[written].lineIndex = index;
        out[written].row = row;
        out[written].inverted = lines[index].selected;
        written++;
    }

    return written;
}

} // namespace meshcompromise
