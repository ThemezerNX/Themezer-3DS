// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include <functional>
#include <optional>
#include <string>

namespace tz::ui
{

// The system keyboard, with its two buttons' labels. Returns the text, or nothing when cancelled. It takes over the
// screens until it closes.
std::optional<std::string> keyboard(const std::string& hint, const std::string& initial, int maxLength,
                                    const std::string& cancel, const std::string& confirm);

// Answers keyboard() instead of the system keyboard, given its hint: for the autopilot, as emulators often lack the
// keyboard applet. Empty to use the system keyboard again.
using KeyboardOverride = std::function<std::optional<std::string>(const std::string& hint)>;
void setKeyboardOverride(KeyboardOverride answer);

} // namespace tz::ui
