#include "meshcompromise/ui_input.h"

namespace meshcompromise
{

UiKey uiKeyFor(const InputEvent *event)
{
    if (event == nullptr)
        return UiKey::None;

    switch (event->inputEvent) {
    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_BACK:
        return UiKey::Cancel;
    case INPUT_BROKER_SELECT_LONG:
        return UiKey::SelectLong;
    case INPUT_BROKER_SELECT:
        return UiKey::Select;
    case INPUT_BROKER_UP:
        return UiKey::Up;
    case INPUT_BROKER_DOWN:
        return UiKey::Down;
    case INPUT_BROKER_LEFT:
        return UiKey::Left;
    case INPUT_BROKER_RIGHT:
        return UiKey::Right;
    default:
        return UiKey::None;
    }
}

} // namespace meshcompromise
