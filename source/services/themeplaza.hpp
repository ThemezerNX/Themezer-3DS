// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// ThemePlaza's catalogue (themeplaza.art), through the API it made for Anemone3DS (v1), over plain HTTP: its HTTPS
// lets only Anemone3DS's user agent through.
//   GET <api>/list?page=<n>&category=1|2|3&query=<search>   (themes, splashes, badges)
//   { "success": true, "pages": 3055, "items": [152151, 152141, ...] }, or { "success": false, "message": ... }
// A page lists only ids; for each, <site>/download/<id>/smdh has its name, author, description and icon,
// <site>/download/<id>/preview its preview and <site>/download/<id> the zip. Its music (/bgm) is Ogg Vorbis, which the
// app can't play, so a preview plays the bgm.bcstm in the zip instead, as the zip downloads.

#pragma once

#include "services/catalog.hpp"

#include <optional>
#include <string>

namespace tz::themeplaza
{

constexpr const char* kSite = "http://themeplaza.art";

std::string pageUrl(catalog::Kind kind, int page, std::string_view query);
// The item of an id: everything but what its SMDH says.
catalog::Item itemOf(std::string_view id);
// InvalidData for anything that isn't a page; a search that finds nothing is an empty page.
Expected<catalog::Page> parsePage(ByteSpan json, int page);
Expected<catalog::Page> fetchPage(net::Client& client, catalog::Kind kind, int page, std::string_view query,
                                  const CancelToken& cancel);

} // namespace tz::themeplaza
