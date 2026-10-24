// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/config.hpp"

#include "core/path.hpp"
#include "sys/log.hpp"

#include <jansson.h>
#include <string_view>

namespace tz
{

namespace
{

const char* sortName(library::Sort sort)
{
    switch (sort)
    {
    case library::Sort::Author:
        return "author";
    case library::Sort::FileName:
        return "file";
    case library::Sort::Newest:
        return "newest";
    case library::Sort::Oldest:
        return "oldest";
    case library::Sort::Badges:
        return "badges";
    case library::Sort::Name:
        break;
    }
    return "name";
}

library::Sort parseSort(const char* name, library::Sort fallback)
{
    if (!name)
        return fallback;
    const std::string_view value(name);
    if (value == "author")
        return library::Sort::Author;
    if (value == "file")
        return library::Sort::FileName;
    if (value == "name")
        return library::Sort::Name;
    if (value == "newest")
        return library::Sort::Newest;
    if (value == "oldest")
        return library::Sort::Oldest;
    if (value == "badges")
        return library::Sort::Badges;
    return fallback;
}

void readBool(json_t* root, const char* key, bool& out)
{
    json_t* value = json_object_get(root, key);
    if (json_is_boolean(value))
        out = json_is_true(value);
}

} // namespace

void moveOldAppFolder(FileSystem& sd)
{
    if (sd.exists(kAppFolder) || !sd.exists(kOldAppFolder))
        return;
    auto entries = sd.list(kOldAppFolder);
    if (!entries || !sd.createDirectories(kAppFolder))
        return;
    size_t moved = 0;
    for (const DirEntry& entry : *entries)
    {
        // a 3dsx kept there stays where the Homebrew Launcher finds it
        if (!entry.isDirectory &&
            (path::hasExtension(entry.name, "3dsx") || path::hasExtension(entry.name, "smdh")))
            continue;
        if (auto renamed = sd.rename(path::join(kOldAppFolder, entry.name), path::join(kAppFolder, entry.name)))
            ++moved;
        else
            log::writeError(entry.name.c_str(), renamed.error());
    }
    if (auto left = sd.list(kOldAppFolder); left && left->empty())
        (void)sd.removeDirectory(kOldAppFolder, false);
    LOG_INFO("moved %u file(s) and folder(s) from %s to %s", static_cast<unsigned>(moved), kOldAppFolder, kAppFolder);
}

Config Config::load(FileSystem& sd)
{
    Config config;
    auto data = sd.readFile(kConfigPath, 64 * 1024);
    if (!data)
        return config;
    json_error_t error;
    json_t* root = json_loadb(reinterpret_cast<const char*>(data->data()), data->size(), 0, &error);
    if (!json_is_object(root))
    {
        json_decref(root);
        return config;
    }
    readBool(root, "backgroundMusic", config.backgroundMusic);
    readBool(root, "previewMusic", config.previewMusic);
    readBool(root, "sounds", config.sounds);
    readBool(root, "checkForUpdates", config.checkForUpdates);
    if (const char* source = json_string_value(json_object_get(root, "browse")))
        config.browseSource = catalog::parseSource(source).value_or(config.browseSource);
    if (const char* order = json_string_value(json_object_get(root, "browseSort")))
        config.browseOrder = catalog::parseOrder(order).value_or(config.browseOrder);
    // the names the app used before this version
    readBool(root, "Background Music", config.backgroundMusic);
    readBool(root, "Check For Updates", config.checkForUpdates);
    config.themeSort = parseSort(json_string_value(json_object_get(root, "themeSort")), config.themeSort);
    config.splashSort = parseSort(json_string_value(json_object_get(root, "splashSort")), config.splashSort);
    config.badgeSort = parseSort(json_string_value(json_object_get(root, "badgeSort")), config.badgeSort);
    if (json_t* tab = json_object_get(root, "lastTab"); json_is_integer(tab))
        config.lastTab = static_cast<int>(json_integer_value(tab));
    json_decref(root);
    return config;
}

Expected<void> Config::save(FileSystem& sd) const
{
    json_t* root = json_object();
    json_object_set_new(root, "backgroundMusic", json_boolean(backgroundMusic));
    json_object_set_new(root, "previewMusic", json_boolean(previewMusic));
    json_object_set_new(root, "sounds", json_boolean(sounds));
    json_object_set_new(root, "checkForUpdates", json_boolean(checkForUpdates));
    json_object_set_new(root, "browse", json_string(catalog::sourceKey(browseSource)));
    json_object_set_new(root, "browseSort", json_string(catalog::orderKey(browseOrder)));
    json_object_set_new(root, "themeSort", json_string(sortName(themeSort)));
    json_object_set_new(root, "splashSort", json_string(sortName(splashSort)));
    json_object_set_new(root, "badgeSort", json_string(sortName(badgeSort)));
    json_object_set_new(root, "lastTab", json_integer(lastTab));
    char* text = json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS);
    json_decref(root);
    if (!text)
        return fail(Errc::OutOfMemory);
    const std::string_view content(text);
    TZ_CHECK(sd.createDirectories(kAppFolder));
    auto result = sd.writeFile(kConfigPath, ByteSpan(reinterpret_cast<const uint8_t*>(content.data()), content.size()));
    free(text);
    return result;
}

} // namespace tz
