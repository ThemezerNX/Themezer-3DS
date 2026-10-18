// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Downloading a theme, splash or badge set: to the SD card, or asking first whether to keep it, install it, or both.

#pragma once

#include "scenes/install.hpp"

#include <functional>
#include <string>

namespace tz::scenes
{

struct Saved
{
    ContentKind kind = ContentKind::Unknown;
    std::string path; // on the SD card
    uint64_t size = 0;
};

// Downloads a zip and saves it in the folder for what it turns out to be (/Themes, /Splashes or /Badges), under name
// when there is one (the catalog's name for it, by which it's recognised later), else the server's name for it, else
// fallbackName. A name already taken gets a number. Shows the progress, and errors.
void downloadToSd(std::string url, std::string title, std::string name, std::string fallbackName,
                  std::function<void(Saved)> done);

// Downloads a zip as downloadToSd() does, then asks what to do with a theme or a splash: save it (Y), install it
// without keeping it (X), or both (A); or nothing (B). A badge set is saved, as they're installed from the Badges tab.
// displayName names it in the question; empty, the name in its file's (see downloadName()).
void downloadAndAsk(std::string url, std::string title, std::string name, std::string fallbackName,
                    std::string displayName);

// Says that a badge set was saved, and where its badges are installed from.
void badgesSaved(const Saved& saved, const std::string& name);

// What a saved download is called: its name in the file's name, as Themezer and ThemePlaza name them, else the file's.
std::string downloadName(const Saved& saved);

} // namespace tz::scenes
