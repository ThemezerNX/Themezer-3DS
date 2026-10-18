// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "ui/overlays.hpp"

#include <string>
#include <vector>

namespace tz::scenes
{

// The menu (START): what the current page offers, then the app's own: the settings, an update, About, and closing
// the app.
void openMenu(std::vector<ui::Action> pageActions);
// A menu of choices under title, such as the orders a list can be in.
void openChoice(std::string title, std::vector<ui::Action> options);

void openSettings();
void openAbout();

} // namespace tz::scenes
