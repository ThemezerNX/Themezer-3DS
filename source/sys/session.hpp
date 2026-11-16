// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// The snapshot of this session on the SD card (domain/session), so that the next one can tell when it ended badly.
// A thread of its own on the system core rewrites it every second or so while something changed: it goes on when the
// main thread hangs, and stops with the app when it crashes (Luma3DS then shows its screen and keeps a dump).

#pragma once

#include "core/io.hpp"
#include "domain/report.hpp"

#include <optional>

namespace tz::session
{

// How the last session ended, when it didn't close normally: a crash, a freeze, a fatal error or an unclean exit (see
// session::ending()), with what it left (its version, memory and log). Reads what it left and keeps it as last-session.txt, so ask once, before begin(). folder is
// the app's on the SD card, where the snapshot is session.txt.
std::optional<report::Report> takeLastSession(FileSystem& sd, std::string_view folder);

// From now until end(), the snapshot says the app is running.
void begin(FileSystem& sd, std::string_view folder);
// The main loop's heartbeat, once a frame: when it stops for long while the app is in front, that's a freeze.
void frame();
// Around what holds the main loop up on purpose, like the system keyboard.
void holdWatch(bool held);
// Writes the snapshot now, for when the app is about to stop on a fatal error; from any thread.
void saveNow();
// The app closes normally: the snapshot goes.
void end();

} // namespace tz::session
