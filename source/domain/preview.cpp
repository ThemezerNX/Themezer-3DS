// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/preview.hpp"

#include "thumbhash/thumbhash.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace tz::preview
{

namespace
{
constexpr int kTopWidth = 400;
constexpr int kBottomWidth = 320;
constexpr int kHeight = 240;

// Fits a picture into a screen-sized canvas, centred on black.
Image fit(const Image& picture, int width, int height)
{
    const float scale = std::min(static_cast<float>(width) / picture.width, static_cast<float>(height) / picture.height);
    const int w = std::max(1, static_cast<int>(picture.width * scale));
    const int h = std::max(1, static_cast<int>(picture.height * scale));
    const Image scaled = resize(picture, w, h);
    Image canvas(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            canvas.at(x, y)[3] = 255;
    const int ox = (width - w) / 2;
    const int oy = (height - h) / 2;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const uint8_t* from = scaled.at(x, y);
            uint8_t* to = canvas.at(ox + x, oy + y);
            const int a = from[3];
            for (int c = 0; c < 3; ++c)
                to[c] = static_cast<uint8_t>(from[c] * a / 255);
        }
    }
    return canvas;
}

// Drawn 32 on its longer side, as the website draws ThumbHashes. Empty when it isn't one.
Image drawThumbHash(const uint8_t* hash, size_t length)
{
    uint8_t* rgba = nullptr;
    int width = 0;
    int height = 0;
    if (!ThumbHashToRGBA(hash, length, 32, &rgba, &width, &height))
        return {};
    Image picture(width, height);
    std::memcpy(picture.pixels.data(), rgba, picture.pixels.size());
    std::free(rgba);
    return picture;
}
} // namespace

Screens split(const Image& picture)
{
    Screens screens;
    const int w = picture.width;
    const int h = picture.height;
    if (w >= kTopWidth && w <= 432 && h >= kHeight * 2 && h <= 528)
    {
        const int border = (w == 432 && h == 528) ? 16 : 0;
        screens.top = crop(picture, (w - kTopWidth) / 2, border, kTopWidth, kHeight);
        screens.bottom = crop(picture, (w - kBottomWidth) / 2, kHeight + border * 2, kBottomWidth, kHeight);
        return screens;
    }
    if (w == kTopWidth && h == kHeight)
    {
        screens.top = picture;
        return screens;
    }
    screens.top = fit(picture, kTopWidth, kHeight);
    return screens;
}

Image blurSource(const Image& picture)
{
    // ThumbHash needs no more than 100x100
    const float scale = std::min(1.0f, 100.0f / std::max(picture.width, picture.height));
    const Image small = resize(picture, std::max(1, static_cast<int>(picture.width * scale)),
                               std::max(1, static_cast<int>(picture.height * scale)));
    uint8_t hash[THUMBHASH_MAX_LENGTH];
    size_t length = 0;
    if (!RGBAToThumbHash(small.width, small.height, small.pixels.data(), hash, &length))
        return {};
    return drawThumbHash(hash, length);
}

Image bothScreens(const Screens& screens)
{
    if (!screens.bottom)
        return screens.top;
    Image both(screens.top.width, screens.top.height + screens.bottom->height);
    const auto place = [&both](const Image& screen, int x0, int y0) {
        for (int y = 0; y < screen.height; ++y)
            for (int x = 0; x < screen.width && x0 + x < both.width; ++x)
                std::memcpy(both.at(x0 + x, y0 + y), screen.at(x, y), 4);
    };
    place(screens.top, 0, 0);
    place(*screens.bottom, std::max(0, (both.width - screens.bottom->width) / 2), screens.top.height);
    return both;
}

Image thumbHashPicture(std::string_view base64)
{
    // zero after its end: the decoder reads as far as the hash's header says, however short it is
    uint8_t hash[THUMBHASH_MAX_LENGTH] = {};
    size_t length = 0;
    uint32_t bits = 0;
    int pending = 0;
    for (const char c : base64)
    {
        int value;
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            value = c - '0' + 52;
        else if (c == '+')
            value = 62;
        else if (c == '/')
            value = 63;
        else if (c == '=')
            break;
        else
            return {};
        bits = (bits << 6) | static_cast<uint32_t>(value);
        pending += 6;
        if (pending >= 8)
        {
            if (length == sizeof(hash))
                return {};
            pending -= 8;
            hash[length++] = static_cast<uint8_t>(bits >> pending);
        }
    }
    return drawThumbHash(hash, length);
}

} // namespace tz::preview
