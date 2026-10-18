// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// The UI's sounds: short clips from the RomFS (romfs/sfx, made by tools/sfx), played from memory on two DSP channels
// of their own, so they sound in the frame they're asked for.

#pragma once

#include <cstdint>

namespace tz::sfx
{

enum class Sound : uint8_t
{
    // The cursor's sounds share a channel: each one cuts the one before, however fast the cursor goes.
    Cursor,
    Page, // a page of a list further, or the next or previous preview
    Tab,
    // The others share the other channel, so of two in one frame the later one plays: a Dialog's Open after the
    // Confirm that opened it.
    Open,
    Close,
    Confirm,
    Back, // a step back, or something taken away
    Done,
    Error,
    Count,
};

// After audio::init(); without the DSP the sounds stay silent.
void init();
// Before audio::exit().
void exit();
void setEnabled(bool enabled);
// On the main thread.
void play(Sound sound);

} // namespace tz::sfx
