// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// What the Browse tab lists, from either site it can browse: Themezer (themezer.net) or ThemePlaza (themeplaza.art).
// Each site's own API is in themezer.hpp and themeplaza.hpp; this is what they have in common.

#pragma once

#include "core/result.hpp"
#include "sys/net.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tz::catalog
{

enum class Source : uint8_t
{
    Themezer,
    ThemePlaza,
};

enum class Kind : uint8_t
{
    Themes,
    Splashes,
    Badges,
};

constexpr int kPageSize = 24;

struct Item
{
    std::string id; // Themezer's quick ID ("T1ED69"), ThemePlaza's number ("152151")
    std::string name;
    std::string author;
    std::string description;
    std::string iconUrl;     // a 48x48 PNG
    // An SMDH with the name, author, description and icon, for a site whose pages list only ids (ThemePlaza): until
    // it's read, the item has none of them.
    std::string detailsUrl;
    std::string previewUrl;  // a PNG, usually both screens (400x480)
    std::string downloadUrl; // a zip
    std::string audioUrl;    // the theme's bgm.bcstm; empty for themes without music (or unknown), splashes and badges
    // A theme's music is only in its download, as bgm.bcstm (ThemePlaza, whose own music previews are Ogg Vorbis): a
    // preview reads it from there, if it has one.
    bool musicInDownload = false;
    // A badge set's badges, where the site says how many (Themezer); 0 where it doesn't.
    int badgeCount = 0;
    // The zip's name on the SD card, made safe for it, ending in .zip. Empty when the site's own name for it is used
    // (ThemePlaza's: "<name> by <author> (<id>).zip").
    std::string filename;
};

struct Page
{
    int page = 1;
    int pages = 1;
    std::vector<Item> items;
};

// What an item's SMDH says about it.
struct Details
{
    std::string name;
    std::string author;
    std::string description;
    std::array<uint16_t, 48 * 48> icon{};
};

// The orders Themezer lists in (app/v1's sort); ThemePlaza lists in its own.
enum class Order : uint8_t
{
    Newest,
    Oldest,
    MostDownloaded,
    LeastDownloaded,
    MostTrending, // by the website's trending score
    LeastTrending,
};
// "newest", "oldest", "most-downloaded", ..., "least-trending": the API's names, and config.json's.
const char* orderKey(Order order);
std::optional<Order> parseOrder(std::string_view key);

// "Themezer", "ThemePlaza"
const char* sourceName(Source source);
// The source's name in config.json.
const char* sourceKey(Source source);
std::optional<Source> parseSource(std::string_view key);

Expected<Page> fetchPage(net::Client& client, Source source, Kind kind, int page, std::string_view query, Order order,
                         const CancelToken& cancel);
// Reads item.detailsUrl.
Expected<Details> fetchDetails(net::Client& client, const Item& item, int language, const CancelToken& cancel);

// Whether a file in the kind's folder on the SD card is this item's download: by item.filename, or without one (the
// site names it, as ThemePlaza does), by the "(<id>)" at the end of its name.
bool isDownloadOf(const Item& item, std::string_view fileName);

} // namespace tz::catalog
