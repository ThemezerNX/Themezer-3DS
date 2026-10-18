// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "core/io.hpp"
#include "scenes/previews.hpp"
#include "ui/scene.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tz::scenes
{

// What a full-size preview shows.
struct PreviewSource
{
    std::string title;
    uint64_t key = 0;
    Previews* previews = nullptr; // the page's previews; it outlives the scene
    Previews::Loader loader;
    Lane lane = Lane::Io;
    // Opens the music, on a worker; empty without music.
    std::function<Expected<std::unique_ptr<StreamReader>>()> music;
    Lane musicLane = Lane::Io;
    // Going through the page's list without closing: whether there's an item step places away (-1 before, +1 after),
    // and that item, which the list then selects. Empty where there's no list to go through.
    std::function<bool(int step)> hasNeighbour;
    std::function<PreviewSource(int step)> neighbour;
};

// A preview on both screens at full size, as the HOME Menu would show it, with the theme's music playing.
std::unique_ptr<ui::Scene> makePreviewScene(PreviewSource source);

} // namespace tz::scenes
