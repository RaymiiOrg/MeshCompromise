#include <gtest/gtest.h>

#include "meshcompromise/ui_nav.h"

using namespace meshcompromise;

namespace
{

// Arbitrary nonzero reference instant for tests that don't care about
// timing - kept away from 0 since UiNavState uses lastSelectMs == 0 as its
// "no pending tap" sentinel.
constexpr uint32_t kNow = 1000;

UiNavState settingsState(uint8_t cursor = 0, bool editing = false)
{
    UiNavState state;
    state.page = UiPage::Settings;
    state.cursor = cursor;
    state.editing = editing;
    return state;
}

UiLine plainLine(const char *text, bool selected)
{
    UiLine line;
    snprintf(line.text, sizeof(line.text), "%s", text);
    line.selected = selected;
    return line;
}

} // namespace

TEST(UiNav, TheStatusPageIgnoresNavigationKeys)
{
    UiNavState state;

    for (UiKey key : {UiKey::Select, UiKey::Up, UiKey::Down, UiKey::Left, UiKey::Right}) {
        const UiNavResult result = navigate(state, key, kNow);
        EXPECT_FALSE(result.handled);
        EXPECT_EQ(UiPage::Status, state.page);
    }
}

TEST(UiNav, ALongSelectOpensSettingsAndReloadsTheDraft)
{
    UiNavState state;

    const UiNavResult result = navigate(state, UiKey::SelectLong, kNow);

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.redraw);
    EXPECT_TRUE(result.reloadDraft);
    EXPECT_EQ(UiPage::Settings, state.page);
    EXPECT_EQ(0, state.cursor);
    EXPECT_FALSE(state.editing);
}

TEST(UiNav, ADoubleTappedSelectOpensSettingsJustLikeALongSelect)
{
    // Keyboard-matrix input (Cardputer, Q10, MPR121, T-Deck...) never raises
    // UiKey::SelectLong - only button/rotary/trackball drivers track press
    // duration - so this double-tap path is the only way those boards can
    // ever reach the Settings page at all.
    UiNavState state;

    const UiNavResult first = navigate(state, UiKey::Select, kNow);
    EXPECT_FALSE(first.handled);
    EXPECT_EQ(UiPage::Status, state.page);

    const UiNavResult second = navigate(state, UiKey::Select, kNow + 200);

    EXPECT_TRUE(second.handled);
    EXPECT_TRUE(second.reloadDraft);
    EXPECT_EQ(UiPage::Settings, state.page);
    EXPECT_EQ(0, state.cursor);
    EXPECT_FALSE(state.editing);
}

TEST(UiNav, ASelectTapThatArrivesTooLateDoesNotOpenSettings)
{
    UiNavState state;

    navigate(state, UiKey::Select, kNow);
    const UiNavResult second = navigate(state, UiKey::Select, kNow + 501);

    EXPECT_FALSE(second.handled);
    EXPECT_EQ(UiPage::Status, state.page);
}

TEST(UiNav, ASecondUnrelatedKeyDoesNotCountAsADoubleTap)
{
    UiNavState state;

    navigate(state, UiKey::Select, kNow);
    navigate(state, UiKey::Up, kNow + 50);
    const UiNavResult third = navigate(state, UiKey::Select, kNow + 100);

    // Up isn't a Select, so it neither consumes nor extends the pending tap;
    // this third call is still within the window of the very first tap and
    // should complete the double-tap.
    EXPECT_TRUE(third.handled);
    EXPECT_EQ(UiPage::Settings, state.page);
}

TEST(UiNav, ALongSelectFromSettingsGoesBackToStatus)
{
    UiNavState state = settingsState(3, true);

    navigate(state, UiKey::SelectLong, kNow);

    EXPECT_EQ(UiPage::Status, state.page);
    EXPECT_EQ(0, state.cursor);
    EXPECT_FALSE(state.editing);
}

TEST(UiNav, CancelOnStatusIsNotOurs)
{
    UiNavState state;

    const UiNavResult result = navigate(state, UiKey::Cancel, kNow);

    EXPECT_FALSE(result.handled);
}

TEST(UiNav, CancelLeavesSettings)
{
    UiNavState state = settingsState(2);

    const UiNavResult result = navigate(state, UiKey::Cancel, kNow);

    EXPECT_TRUE(result.handled);
    EXPECT_EQ(UiPage::Status, state.page);
}

TEST(UiNav, CancelWhileEditingCommitsInsteadOfLeaving)
{
    UiNavState state = settingsState(2, true);

    const UiNavResult result = navigate(state, UiKey::Cancel, kNow);

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.commit);
    EXPECT_FALSE(state.editing);
    EXPECT_EQ(UiPage::Settings, state.page);
}

TEST(UiNav, SelectTogglesEditingAndCommitsOnTheWayOut)
{
    UiNavState state = settingsState(1);

    const UiNavResult enter = navigate(state, UiKey::Select, kNow);
    EXPECT_TRUE(state.editing);
    EXPECT_FALSE(enter.commit);

    const UiNavResult leave = navigate(state, UiKey::Select, kNow);
    EXPECT_FALSE(state.editing);
    EXPECT_TRUE(leave.commit);
}

TEST(UiNav, UpAndDownMoveTheCursorWhenNotEditing)
{
    UiNavState state = settingsState(1);

    navigate(state, UiKey::Down, kNow);
    EXPECT_EQ(2, state.cursor);

    navigate(state, UiKey::Up, kNow);
    EXPECT_EQ(1, state.cursor);
}

TEST(UiNav, TheCursorStopsAtBothEnds)
{
    UiNavState state = settingsState(0);

    navigate(state, UiKey::Up, kNow);
    EXPECT_EQ(0, state.cursor);

    state.cursor = static_cast<uint8_t>(SettingField::Count) - 1;
    navigate(state, UiKey::Down, kNow);
    EXPECT_EQ(static_cast<uint8_t>(SettingField::Count) - 1, state.cursor);
}

TEST(UiNav, UpAndDownAdjustTheValueWhileEditing)
{
    UiNavState state = settingsState(4, true);

    const UiNavResult up = navigate(state, UiKey::Up, kNow);
    EXPECT_EQ(1, up.adjust);
    EXPECT_EQ(4, state.cursor);

    const UiNavResult down = navigate(state, UiKey::Down, kNow);
    EXPECT_EQ(-1, down.adjust);
    EXPECT_EQ(4, state.cursor);
}

TEST(UiNav, LeftAndRightAdjustWithoutEnteringEditMode)
{
    UiNavState state = settingsState(2);

    EXPECT_EQ(-1, navigate(state, UiKey::Left, kNow).adjust);
    EXPECT_EQ(1, navigate(state, UiKey::Right, kNow).adjust);
    EXPECT_FALSE(state.editing);
}

TEST(UiNav, AnUnknownKeyChangesNothing)
{
    UiNavState state = settingsState(2, true);

    const UiNavResult result = navigate(state, UiKey::None, kNow);

    EXPECT_FALSE(result.handled);
    EXPECT_EQ(2, state.cursor);
    EXPECT_TRUE(state.editing);
}

TEST(UiRows, TheFirstLineIsTheHeaderAndIsNeverDrawnAsABodyRow)
{
    const UiLine lines[] = {plainLine("MeshCore", false), plainLine("one", false), plainLine("two", false)};
    UiRowPlan plan[6];

    const size_t planned = planRows(lines, 3, 1, 6, plan, 6);

    ASSERT_EQ(2u, planned);
    EXPECT_EQ(1u, plan[0].lineIndex);
    EXPECT_EQ(1, plan[0].row);
    EXPECT_EQ(2u, plan[1].lineIndex);
    EXPECT_EQ(2, plan[1].row);
}

TEST(UiRows, OnlyTheSelectedLineIsInverted)
{
    const UiLine lines[] = {plainLine("MeshCore", false), plainLine("one", false), plainLine("two", true),
                            plainLine("three", false)};
    UiRowPlan plan[6];

    const size_t planned = planRows(lines, 4, 1, 6, plan, 6);

    ASSERT_EQ(3u, planned);
    EXPECT_FALSE(plan[0].inverted);
    EXPECT_TRUE(plan[1].inverted);
    EXPECT_FALSE(plan[2].inverted);
}

TEST(UiRows, TheBodyNeverOverflowsTheAvailableRows)
{
    UiLine lines[12];
    for (size_t i = 0; i < 12; i++)
        lines[i] = plainLine("line", false);
    UiRowPlan plan[6];

    const size_t planned = planRows(lines, 12, 1, 6, plan, 6);

    EXPECT_EQ(6u, planned);
    EXPECT_EQ(6, plan[5].row);
}

TEST(UiRows, AWindowedStartSkipsScrolledOffLines)
{
    UiLine lines[12];
    for (size_t i = 0; i < 12; i++)
        lines[i] = plainLine("line", i == 9);
    UiRowPlan plan[6];

    const size_t planned = planRows(lines, 12, 5, 6, plan, 6);

    ASSERT_EQ(6u, planned);
    EXPECT_EQ(5u, plan[0].lineIndex);
    EXPECT_TRUE(plan[4].inverted);
}

TEST(UiRows, NothingIsPlannedForAnEmptyPage)
{
    UiRowPlan plan[6];
    const UiLine lines[] = {plainLine("MeshCore", false)};

    EXPECT_EQ(0u, planRows(lines, 1, 1, 6, plan, 6));
    EXPECT_EQ(0u, planRows(nullptr, 4, 1, 6, plan, 6));
}

TEST(UiRows, EveryVisibleSettingsRowIsReachableByScrolling)
{
    UiLine lines[1 + static_cast<size_t>(SettingField::Count)];
    lines[0] = plainLine("Settings", false);

    for (uint8_t cursor = 0; cursor < static_cast<uint8_t>(SettingField::Count); cursor++) {
        for (size_t i = 1; i < sizeof(lines) / sizeof(lines[0]); i++)
            lines[i] = plainLine("field", i == static_cast<size_t>(cursor) + 1);

        UiRowPlan plan[6];
        const size_t planned =
            planRows(lines, sizeof(lines) / sizeof(lines[0]), visibleWindowStart(cursor, 6), 6, plan, 6);

        bool found = false;
        for (size_t i = 0; i < planned; i++)
            found = found || plan[i].inverted;

        EXPECT_TRUE(found) << "cursor " << static_cast<int>(cursor) << " scrolled out of view";
    }
}
