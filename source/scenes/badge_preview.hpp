// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "domain/badges.hpp"
#include "ui/scene.hpp"

#include <functional>
#include <memory>

namespace tz::scenes
{

// The set a badge preview shows, and the page's list around it.
struct BadgePreviewSource
{
    badges::SetSource set;
    // As PreviewSource's: whether there's a set step places away (-1 before, +1 after), and that set, which the list
    // then selects. Empty where there's no list to go through.
    std::function<bool(int step)> hasNeighbour;
    std::function<BadgePreviewSource(int step)> neighbour;
};

// All of a set's badges, in five columns as wide as the bottom screen that run on from the top screen onto it, shown
// as they're read and scrolled with up and down or a drag.
std::unique_ptr<ui::Scene> makeBadgePreview(BadgePreviewSource source);

} // namespace tz::scenes
