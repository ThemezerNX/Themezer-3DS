// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "ui/keyboard.hpp"

#include "sys/session.hpp"

#include <3ds.h>
#include <vector>

namespace tz::ui
{

namespace
{
KeyboardOverride keyboardOverride;
} // namespace

void setKeyboardOverride(KeyboardOverride answer)
{
    keyboardOverride = std::move(answer);
}

std::optional<std::string> keyboard(const std::string& hint, const std::string& initial, int maxLength,
                                    const std::string& cancel, const std::string& confirm)
{
    if (keyboardOverride)
    {
        std::optional<std::string> answer = keyboardOverride(hint);
        if (answer && answer->size() > static_cast<size_t>(maxLength))
            answer->resize(static_cast<size_t>(maxLength));
        return answer;
    }
    SwkbdState state;
    swkbdInit(&state, SWKBD_TYPE_NORMAL, 2, maxLength);
    swkbdSetHintText(&state, hint.c_str());
    swkbdSetInitialText(&state, initial.c_str());
    swkbdSetButton(&state, SWKBD_BUTTON_LEFT, cancel.c_str(), false);
    swkbdSetButton(&state, SWKBD_BUTTON_RIGHT, confirm.c_str(), true);
    swkbdSetFeatures(&state, SWKBD_PREDICTIVE_INPUT);
    // the UTF-8 result can take up to four bytes for each character
    std::vector<char> text(static_cast<size_t>(maxLength) * 4 + 1, '\0');
    // the main loop waits for the keyboard: not a freeze
    session::holdWatch(true);
    const SwkbdButton button = swkbdInputText(&state, text.data(), text.size());
    session::holdWatch(false);
    if (button != SWKBD_BUTTON_CONFIRM)
        return std::nullopt;
    return std::string(text.data());
}

} // namespace tz::ui
