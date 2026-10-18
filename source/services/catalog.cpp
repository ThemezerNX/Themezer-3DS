// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "services/catalog.hpp"

#include "core/path.hpp"
#include "core/smdh.hpp"
#include "services/themeplaza.hpp"
#include "services/themezer.hpp"

namespace tz::catalog
{

const char* sourceName(Source source)
{
    return source == Source::ThemePlaza ? "ThemePlaza" : "Themezer";
}

const char* orderKey(Order order)
{
    switch (order)
    {
    case Order::Oldest:
        return "oldest";
    case Order::MostDownloaded:
        return "most-downloaded";
    case Order::LeastDownloaded:
        return "least-downloaded";
    case Order::MostTrending:
        return "most-trending";
    case Order::LeastTrending:
        return "least-trending";
    case Order::Newest:
        break;
    }
    return "newest";
}

std::optional<Order> parseOrder(std::string_view key)
{
    for (Order order : {Order::Newest, Order::Oldest, Order::MostDownloaded, Order::LeastDownloaded, Order::MostTrending,
                        Order::LeastTrending})
        if (key == orderKey(order))
            return order;
    return std::nullopt;
}

const char* sourceKey(Source source)
{
    return source == Source::ThemePlaza ? "themeplaza" : "themezer";
}

std::optional<Source> parseSource(std::string_view key)
{
    for (Source source : {Source::Themezer, Source::ThemePlaza})
        if (key == sourceKey(source))
            return source;
    return std::nullopt;
}

Expected<Page> fetchPage(net::Client& client, Source source, Kind kind, int page, std::string_view query, Order order,
                         const CancelToken& cancel)
{
    if (source == Source::ThemePlaza)
        return themeplaza::fetchPage(client, kind, page, query, cancel);
    return themezer::fetchPage(client, kind, page, query, order, cancel);
}

Expected<Details> fetchDetails(net::Client& client, const Item& item, int language, const CancelToken& cancel)
{
    if (item.detailsUrl.empty())
        return fail(Errc::NotFound, "no details for " + item.id);
    net::Request request;
    request.url = item.detailsUrl;
    request.maxSize = 64 * 1024;
    request.cancel = &cancel;
    const net::Response response = TZ_TRY(client.get(request));
    const Smdh smdh = TZ_TRY(parseSmdh(response.body));
    const SmdhText& text = smdh.title(language);
    Details details;
    details.name = text.name;
    details.author = text.author;
    details.description = text.description;
    details.icon = smdh.largeIcon;
    return details;
}

bool isDownloadOf(const Item& item, std::string_view fileName)
{
    if (!item.filename.empty())
        return path::equalsIgnoreCase(fileName, item.filename);
    // the site's own name: "<name> by <author> (<id>).zip", or "... (<id>) (2).zip" for a second copy
    if (item.id.empty() || !path::hasExtension(fileName, "zip"))
        return false;
    const std::string tag = " (" + item.id + ")";
    const std::string_view stem = path::stem(fileName);
    if (stem.ends_with(tag))
        return true;
    const size_t copy = stem.rfind(" (");
    return copy != std::string_view::npos && stem.substr(0, copy).ends_with(tag);
}

} // namespace tz::catalog
