// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// The settings, in /3ds/themezer-3ds/config.json, and the app's other files on the SD card.

#pragma once

#include "core/io.hpp"
#include "domain/library.hpp"
#include "services/catalog.hpp"

namespace tz
{

#define TZ_APP_FOLDER "/3ds/" APP_NAME

constexpr const char* kAppFolder = TZ_APP_FOLDER;
constexpr const char* kConfigPath = TZ_APP_FOLDER "/config.json";
constexpr const char* kCacheFolder = TZ_APP_FOLDER "/cache";
constexpr const char* kBadgeBackupFolder = TZ_APP_FOLDER "/badge-backup";
constexpr const char* kBadgeExportFolder = TZ_APP_FOLDER "/Badge exports";
// what went wrong, for a bug report; written with stdio, hence sdmc:
constexpr const char* kLogPath = "sdmc:" TZ_APP_FOLDER "/log.txt";
// The folder of the app's first builds: what's in it moves to kAppFolder once, but the app itself if it's there.
constexpr const char* kOldAppFolder = "/3ds/Themezer";

// Moves the files in kOldAppFolder to kAppFolder, when there's no kAppFolder yet.
void moveOldAppFolder(FileSystem& sd);

struct Config
{
    bool backgroundMusic = true;
    bool previewMusic = true; // a theme's music plays in its preview
    bool sounds = true;       // the UI's own sounds
    bool checkForUpdates = true;
    catalog::Source browseSource = catalog::Source::Themezer;
    catalog::Order browseOrder = catalog::Order::Newest;
    library::Sort themeSort = library::Sort::Name;
    library::Sort splashSort = library::Sort::Name;
    int lastTab = 0;

    // Missing or broken settings keep their defaults.
    static Config load(FileSystem& sd);
    Expected<void> save(FileSystem& sd) const;
};

} // namespace tz
