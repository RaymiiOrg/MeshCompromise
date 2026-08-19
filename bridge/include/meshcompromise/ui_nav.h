#pragma once

#include <cstddef>
#include <cstdint>

#include "meshcompromise/ui_text.h"

namespace meshcompromise
{

enum class UiPage { Status, Settings };

enum class UiKey { None, Select, SelectLong, Cancel, Up, Down, Left, Right };

struct UiNavState {
    UiPage page = UiPage::Status;
    uint8_t cursor = 0;
    bool editing = false;
    // Set on a Select tap while on the Status page, cleared once consumed by
    // a follow-up tap or once it ages out. See navigate()'s double-tap entry
    // path: keyboard input (Cardputer, Q10, MPR121, T-Deck...) never
    // generates UiKey::SelectLong, only button/rotary/trackball hardware
    // does, so a keyboard needs another way to reach the Settings page.
    uint32_t lastSelectMs = 0;
};

struct UiNavResult {
    bool handled = false;
    bool redraw = false;
    bool commit = false;
    bool reloadDraft = false;
    int8_t adjust = 0;
};

UiNavResult navigate(UiNavState &state, UiKey key, uint32_t nowMs);

struct UiRowPlan {
    size_t lineIndex = 0;
    uint8_t row = 0;
    bool inverted = false;
};

size_t planRows(const UiLine *lines, size_t count, size_t first, size_t bodyRows, UiRowPlan *out, size_t max);

} // namespace meshcompromise
