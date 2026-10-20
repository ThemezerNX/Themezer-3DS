// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/splashes.hpp"

#include "core/path.hpp"
#include "core/zip.hpp"

#include <algorithm>

namespace tz::splashes
{

namespace
{

std::optional<Fingerprint> fingerprintOf(FileSystem& sd, const char* path)
{
    auto data = sd.readFile(path, kMaxSize);
    if (!data || data->empty())
        return std::nullopt;
    return Fingerprint{static_cast<uint32_t>(data->size()), crc32(*data)};
}

Expected<void> replace(FileSystem& sd, const char* path, ByteSpan data)
{
    if (data.size() > kMaxSize)
        return fail(Errc::TooLarge, std::string(path) + " too large");
    return sd.writeFile(path, data);
}

Expected<void> removeIfThere(FileSystem& sd, const char* path)
{
    auto removed = sd.removeFile(path);
    if (!removed && removed.error().code != Errc::NotFound)
        return removed;
    return {};
}

// Written beside the old file first: Luma3DS goes back to its defaults for all its settings when its configuration
// doesn't read.
Expected<void> replaceConfig(FileSystem& sd, const char* path, ByteSpan data)
{
    const std::string part = std::string(path) + ".part";
    TZ_CHECK(sd.writeFile(part, data));
    TZ_CHECK(sd.removeFile(path));
    return sd.rename(part, path);
}

// v11 and later read an INI file at boot, and any line it doesn't understand resets it, so only the one line changes.
Expected<bool> turnOnInIni(FileSystem& sd)
{
    auto config = sd.readFile(kLumaIniPath, 0x4000);
    if (!config)
        return false;
    std::string text(config->begin(), config->end());
    for (size_t start = 0; start < text.size();)
    {
        const size_t end = std::min(text.find('\n', start), text.size());
        std::string line = text.substr(start, end - start);
        std::erase_if(line, [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
        if (path::equalsIgnoreCase(line, "splash_position=off"))
        {
            const size_t cr = text[end - 1] == '\r' ? 1 : 0;
            text.replace(start, end - cr - start, "splash_position = before payloads");
            const ByteSpan data(reinterpret_cast<const uint8_t*>(text.data()), text.size());
            TZ_CHECK(replaceConfig(sd, kLumaIniPath, data));
            return true;
        }
        start = end + 1;
    }
    return false;
}

// v8.1 to v10.3: "CONF", the format version (2.x), then the options as bits. The multi-choice options take two bits
// each in the word at 0xC; the splash is the third of them (off, before payloads, after payloads). Older versions
// pack them elsewhere and are left alone.
Expected<bool> turnOnInBin(FileSystem& sd)
{
    auto config = sd.readFile(kLumaBinPath, 0x1000);
    if (!config || config->size() < 0x10 || !startsWith(*config, "CONF") || readU16(*config, 4) != 2)
        return false;
    const uint32_t multiOptions = readU32(*config, 0xC);
    if (((multiOptions >> 4) & 3) != 0)
        return false;
    writeU32(config->data() + 0xC, multiOptions | 1u << 4);
    TZ_CHECK(replaceConfig(sd, kLumaBinPath, *config));
    return true;
}

} // namespace

Expected<Current> readCurrent(FileSystem& sd)
{
    return Current{fingerprintOf(sd, kTopPath), fingerprintOf(sd, kBottomPath)};
}

Expected<void> install(FileSystem& sd, ByteSpan top, ByteSpan bottom, Screens screens)
{
    const bool wantTop = screens != Screens::Bottom;
    const bool wantBottom = screens != Screens::Top;
    if ((!wantTop || top.empty()) && (!wantBottom || bottom.empty()))
        return fail(Errc::NotFound, "no splash for these screens");

    TZ_CHECK(sd.createDirectories("/luma"));
    if (wantTop)
    {
        if (!top.empty())
            TZ_CHECK(replace(sd, kTopPath, top));
        else if (screens == Screens::Both)
            TZ_CHECK(removeIfThere(sd, kTopPath));
    }
    if (wantBottom)
    {
        if (!bottom.empty())
            TZ_CHECK(replace(sd, kBottomPath, bottom));
        else if (screens == Screens::Both)
            TZ_CHECK(removeIfThere(sd, kBottomPath));
    }
    return {};
}

Expected<void> uninstall(FileSystem& sd)
{
    TZ_CHECK(removeIfThere(sd, kTopPath));
    return removeIfThere(sd, kBottomPath);
}

Expected<Files> readInstalled(FileSystem& sd)
{
    Files files;
    if (auto top = sd.readFile(kTopPath, kMaxSize))
        files.top = std::move(*top);
    if (auto bottom = sd.readFile(kBottomPath, kMaxSize))
        files.bottom = std::move(*bottom);
    if (files.top.empty() && files.bottom.empty())
        return fail(Errc::NotFound, "no splash installed");
    return files;
}

Expected<bool> turnOnInLuma(FileSystem& sd)
{
    // Both, should Luma3DS have been updated or downgraded since: it doesn't read the other one.
    const bool ini = TZ_TRY(turnOnInIni(sd));
    const bool bin = TZ_TRY(turnOnInBin(sd));
    return ini || bin;
}

std::optional<Image> toImage(ByteSpan data, bool top)
{
    const int width = top ? 400 : 320;
    const int height = 240;
    if (data.size() != static_cast<size_t>(width) * height * 3)
        return std::nullopt;

    // The screens' own layout: the picture turned a quarter, one column of 240 pixels at a time from the left,
    // each from the bottom up, in BGR.
    Image image(width, height);
    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* source = data.data() + (static_cast<size_t>(x) * height + (height - 1 - y)) * 3;
            uint8_t* pixel = image.at(x, y);
            pixel[0] = source[2];
            pixel[1] = source[1];
            pixel[2] = source[0];
            pixel[3] = 0xFF;
        }
    }
    return image;
}

} // namespace tz::splashes
