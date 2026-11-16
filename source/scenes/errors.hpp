// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// What the app tells the user when something fails: what happened and what to do, in plain words.

#pragma once

#include "core/result.hpp"
#include "domain/item_ids.hpp"

#include <optional>
#include <string>

namespace tz::scenes
{

struct ErrorInfo
{
    std::string title;
    std::string message;
    // The error, when the app is to blame for it (see report::shareable()): the dialog then offers to share it.
    std::optional<Error> report;
};

// Any error, in general terms.
ErrorInfo describe(const Error& error);
// Installing something called name failed. When it's from another site than Themezer, a damaged or unsupported file is
// that site's, not the app's to answer for: the dialog says where it's from and offers nothing to share.
ErrorInfo installError(const Error& error, const std::string& name, ids::Site site = ids::Site::Themezer);
// The HOME Menu's theme storage can't be used.
ErrorInfo themeStorageError(const Error& error);
// A download failed.
ErrorInfo downloadError(const Error& error);
// Why the HOME Menu's badges can't be changed.
ErrorInfo badgeStorageError(const Error& error);

// Shows it in a dialog, with a button to share the error when it has one to report.
void showError(const ErrorInfo& info);

} // namespace tz::scenes
