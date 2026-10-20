// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Luma3DS's splash screens: /luma/splash.bin (top screen) and /luma/splashbottom.bin (bottom screen), shown while
// the console starts.

#pragma once

#include "core/image.hpp"
#include "core/io.hpp"

#include <optional>

namespace tz::splashes
{

constexpr const char* kTopPath = "/luma/splash.bin";
constexpr const char* kBottomPath = "/luma/splashbottom.bin";
constexpr const char* kTopName = "splash.bin";
constexpr const char* kBottomName = "splashbottom.bin";
// Luma3DS's configuration: an INI file from v11 on, a binary one in v8.1 to v10.3.
constexpr const char* kLumaIniPath = "/luma/config.ini";
constexpr const char* kLumaBinPath = "/luma/config.bin";

// A splash that is raw screen data: 3 bytes a pixel.
constexpr uint32_t kTopRawSize = 400 * 240 * 3;
constexpr uint32_t kBottomRawSize = 320 * 240 * 3;
// Luma doesn't read anything larger.
constexpr uint32_t kMaxSize = 0x80000;

enum class Screens : uint8_t
{
    Both,
    Top,
    Bottom,
};

struct Fingerprint
{
    uint32_t size = 0;
    uint32_t crc = 0;
    bool operator==(const Fingerprint&) const = default;
};

struct Current
{
    std::optional<Fingerprint> top;
    std::optional<Fingerprint> bottom;
};

Expected<Current> readCurrent(FileSystem& sd);

// Installs the given screens. A screen whose file is empty is left as it is. With Screens::Both, a splash that has
// only one screen removes the other screen's old splash, so the two always belong together.
Expected<void> install(FileSystem& sd, ByteSpan top, ByteSpan bottom, Screens screens);
Expected<void> uninstall(FileSystem& sd);

// The installed splashes, to save them to the SD card. NotFound when there are none.
struct Files
{
    Bytes top;
    Bytes bottom;
};
Expected<Files> readInstalled(FileSystem& sd);

// Turns on Luma3DS's splashes when its configuration has them off ("Splash: Off" in its boot menu), shown before
// payloads like the menu's first choice. Whether it changed anything: false too when there is no configuration.
Expected<bool> turnOnInLuma(FileSystem& sd);

// A raw splash as a picture, for previews of splashes that come without one. nullopt for other formats.
std::optional<Image> toImage(ByteSpan data, bool top);

} // namespace tz::splashes
