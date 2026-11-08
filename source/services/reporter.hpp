// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Sharing an error with Themezer, when the user presses the dialog's button (see domain/report.hpp for what is
// offered, and nothing is ever sent otherwise). The report is
//   POST <api>/reports   Content-Type: application/json   (the body is domain/report.hpp's toJson())
// answered with any 2xx. Themezer only logs it, to look the error up later. It holds no ID of the console or the user:
// the app's version, the console's model, region and firmware, the error and the last lines of the app's log.

#pragma once

#include "core/result.hpp"

namespace tz::report
{

// Sends the error in the background, and says in a toast how it went.
void share(const Error& error);

} // namespace tz::report
