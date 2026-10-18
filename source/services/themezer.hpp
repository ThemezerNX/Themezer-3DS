// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Themezer's catalogue, through the API Themezer serves this app (app/v1; its lists answer as Anemone3DS's anemone/v2):
//   GET <api>/themes|splashes|badges?page=<n>&q=<search>&sort=<order>
//   { "page": 1, "pages": 3, "items": [ { "id", "name", "author", "description", "iconUrl", "previewUrl",
//     "downloadUrl", "audioUrl", "filename" } ] }
//   GET <api>/quickid/<quick ID>
//   { "kind": "themes" | "splashes" | "badges", "item": { ...as in a page } }, or 404

#pragma once

#include "services/catalog.hpp"

#include <optional>
#include <string>

namespace tz::themezer
{

constexpr const char* kApiUrl = "https://3ds.themezer.net/app/v1";
constexpr const char* kWebsite = "https://themezer.net";

std::string pageUrl(catalog::Kind kind, int page, std::string_view query, catalog::Order order);
// Items without an id or a download URL are left out. InvalidData for anything that isn't such a page.
Expected<catalog::Page> parsePage(ByteSpan json);
Expected<catalog::Page> fetchPage(net::Client& client, catalog::Kind kind, int page, std::string_view query,
                                  catalog::Order order, const CancelToken& cancel);

struct Found
{
    catalog::Kind kind = catalog::Kind::Themes;
    catalog::Item item;
};

// NotFound when Themezer has nothing public with that quick ID (see ids::themezerQuickId()).
Expected<Found> parseQuickId(ByteSpan json);
Expected<Found> findQuickId(net::Client& client, std::string_view quickId, const CancelToken& cancel);

} // namespace tz::themezer
