// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// How the last session ended. While the app runs, sys/session keeps a snapshot of it on the SD card, and removes it
// when the app closes normally: one still there when the app starts is from a session that ended otherwise, by a
// crash, a freeze or the power going. Luma3DS's dump of a crash says where it happened.

#pragma once

#include "core/bytes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tz::session
{

// The app as it was when the snapshot was written, every second or so.
struct Snapshot
{
    std::string version;
    uint64_t titleId = 0;
    uint32_t lumaDumps = 0; // the highest number among Luma's ARM11 crash dumps when the session started
    uint32_t stalledMs = 0; // how long the main loop had stopped, while the app was in front
    // in KB, as in a report
    uint32_t heap = 0;
    uint32_t heapUsed = 0;
    uint32_t linear = 0;
    uint32_t linearFree = 0;
    std::vector<std::string> log;
};
std::string toText(const Snapshot& snapshot);
// Nothing for what isn't one (cut short before its log, say).
std::optional<Snapshot> parse(std::string_view text);

// One of Luma3DS's ARM11 exception dumps, /luma/dumps/arm11/crash_dump_<n>.dmp: a header, the registers, a bit of
// code and stack, then the process's name and title ID.
struct LumaDump
{
    uint32_t type = 0; // 0 FIQ, 1 undefined instruction, 2 prefetch abort, 3 data abort
    std::array<uint32_t, 13> r{};
    uint32_t sp = 0;
    uint32_t lr = 0;
    uint32_t pc = 0;
    uint32_t cpsr = 0;
    uint32_t far = 0; // the address a data abort tried
    std::string process;
    uint64_t titleId = 0;
};
std::optional<LumaDump> parseLumaDump(ByteSpan data);
// 39 for "crash_dump_00000039.dmp": Luma counts them up, in decimal (00000040 comes next).
std::optional<uint32_t> lumaDumpNumber(std::string_view fileName);

// What a report says of it: "crash" (Luma's dump of this app came after it started), "freeze" (the main loop had
// stopped for kFreezeMs or more), "fatal error" (the app stopped itself, main.cpp's onTerminate()), and the details;
// else "unclean exit": a crash whose dump wasn't saved (Luma saves it only on A), or the power going.
constexpr uint32_t kFreezeMs = 10000;
struct Ending
{
    std::string kind;
    std::string detail;
};
Ending ending(const Snapshot& snapshot, const std::optional<LumaDump>& dump);

} // namespace tz::session
