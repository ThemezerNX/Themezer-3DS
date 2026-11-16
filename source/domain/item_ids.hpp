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

enum class Site
{
    Themezer,
    ThemePlaza,
    Elsewhere,
};

// The ID a file's name carries the way the sites name their downloads, "<name> Theme by <author> (Themezer-T1ED69)"
// and "<name> by <author> (152151)": Themezer's tag in any case, with a quick ID or the plain hexadecimal one
// ("(Themezer-1ED69)"), or ThemePlaza's 3 to 9 digits, anywhere in the name
// so that a download is recognised whatever the rest of its name is; the last one, so that a copy's " (2)" after it
// doesn't count, and Themezer's over a number. Every reading of a file's name goes through this.
struct IdInName
{
    Site site = Site::Elsewhere;
    std::string id;   // as its site writes it: "T1ED69", "152151"
    size_t start = 0; // the tag's place in the name, "(" to ")"
    size_t end = 0;
};
std::optional<IdInName> idInFileName(std::string_view fileName);

// Whether a file's name carries this item's ID (see idInFileName()).
bool fileNameHasId(std::string_view fileName, std::string_view id);
// Where an item's file is from, by its name (see idInFileName()): elsewhere when it carries no ID.
Site siteOfFileName(std::string_view fileName);

// The ID in a link to one of ThemePlaza's items ("https://themeplaza.art/download/152151", ".../item/152151").
std::optional<std::string> themePlazaIdInLink(std::string_view url);

} // namespace tz::ids
