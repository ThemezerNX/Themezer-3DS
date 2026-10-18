// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// The IDs people type or scan for an item on Themezer or ThemePlaza.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tz::ids
{

// A Themezer quick ID as typed: trimmed and in capitals, when it looks like one: letters for the kind, then a number
// in hexadecimal ("T1ED69", "S1EBAA", "BS1B98F").
std::optional<std::string> themezerQuickId(std::string_view text);

// A ThemePlaza ID as typed: digits only ("152151", or "#152151"), without leading zeros.
std::optional<std::string> themePlazaId(std::string_view text);

// Whether a file's name carries an item's ID the way the sites name downloads: "(Themezer-T1ED69)" or "(152151)",
// anywhere in it and in any case, so that a download is recognised whatever the rest of its name is.
bool fileNameHasId(std::string_view fileName, std::string_view id);

// The ID in a link to one of ThemePlaza's items ("https://themeplaza.art/download/152151", ".../item/152151").
std::optional<std::string> themePlazaIdInLink(std::string_view url);

} // namespace tz::ids
