// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// An error report: what the user shares when an unexpected error's dialog offers it. Only errors the app has to answer
// for are offered (a broken file, the HOME Menu's storage); not the ones outside it (the network, a full SD card).

#pragma once

#include "core/result.hpp"

#include <string>
#include <vector>

namespace tz::report
{

// Whether the app is to blame for an error, or at least the one to look into it: the network, a full SD card, the
// user's own cancelling or a file too big for the HOME Menu are not.
bool shareable(Errc code);

struct Report
{
    std::string version;  // of the app
    std::string device;   // "New 3DS XL"
    std::string region;   // "EUR"
    std::string firmware; // "11.17.0"
    Error error;
    std::vector<std::string> log; // the last lines of the app's log
};

// The JSON the server takes: { "version", "device", "region", "firmware", "error": "invalid data" (errcName), "system": "E0E083F2",
// "detail", "log": [ ... ] }. Valid UTF-8 whatever the texts hold, and no larger than a few KiB.
std::string toJson(const Report& report);

} // namespace tz::report
