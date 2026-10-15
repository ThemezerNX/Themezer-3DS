// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "services/themeplaza.hpp"

#include <algorithm>
#include <jansson.h>
#include <memory>

namespace tz::themeplaza
{

namespace
{

constexpr uint64_t kMaxPageSize = 256 * 1024;

struct JsonDelete
{
    void operator()(json_t* json) const { json_decref(json); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDelete>;

int category(catalog::Kind kind)
{
    switch (kind)
    {
    case catalog::Kind::Themes:
        return 1;
    case catalog::Kind::Splashes:
        return 2;
    case catalog::Kind::Badges:
        return 3;
    }
    return 1;
}

} // namespace

std::string pageUrl(catalog::Kind kind, int page, std::string_view query)
{
    return std::string(kSite) + "/api/anemone/v1/list?page=" + std::to_string(page) +
           "&category=" + std::to_string(category(kind)) + "&query=" + net::urlEncode(query);
}

catalog::Item itemOf(std::string_view id)
{
    const std::string base = std::string(kSite) + "/download/" + std::string(id);
    catalog::Item item;
    item.id = std::string(id);
    item.detailsUrl = base + "/smdh";
    item.previewUrl = base + "/preview";
    item.downloadUrl = base;
    return item;
}

Expected<catalog::Page> parsePage(ByteSpan json, int page)
{
    json_error_t error;
    const JsonPtr root(json_loadb(reinterpret_cast<const char*>(json.data()), json.size(), 0, &error));
    if (!json_is_object(root.get()))
        return fail(Errc::InvalidData, std::string("ThemePlaza page: ") + error.text);

    catalog::Page result;
    result.page = page;
    // "success": false with "No items found" is how a search that finds nothing answers
    if (!json_is_true(json_object_get(root.get(), "success")))
    {
        result.pages = page;
        return result;
    }
    json_t* items = json_object_get(root.get(), "items");
    if (!json_is_array(items))
        return fail(Errc::InvalidData, "ThemePlaza page without items");
    json_t* pages = json_object_get(root.get(), "pages");
    const json_int_t count = json_is_integer(pages) ? json_integer_value(pages) : page;
    result.pages = static_cast<int>(std::clamp<json_int_t>(count, page, 1 << 20));

    size_t index = 0;
    json_t* value = nullptr;
    json_array_foreach(items, index, value)
    {
        if (!json_is_integer(value) || json_integer_value(value) <= 0)
            continue;
        result.items.push_back(itemOf(std::to_string(json_integer_value(value))));
    }
    return result;
}

Expected<catalog::Page> fetchPage(net::Client& client, catalog::Kind kind, int page, std::string_view query,
                                  const CancelToken& cancel)
{
    net::Request request;
    request.url = pageUrl(kind, page, query);
    request.accept = {"application/json"};
    request.maxSize = kMaxPageSize;
    request.cancel = &cancel;
    const net::Response response = TZ_TRY(client.get(request));
    catalog::Page result = TZ_TRY(parsePage(response.body, page));
    for (catalog::Item& item : result.items)
        item.musicInDownload = kind == catalog::Kind::Themes;
    return result;
}

} // namespace tz::themeplaza
