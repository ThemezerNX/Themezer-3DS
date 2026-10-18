// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "gfx/draw.hpp"

#include "brand.h"
#include "gfx/renderer.hpp"
#include "icons.h"

#include <algorithm>
#include <citro2d.h>
#include <cmath>
#include <iterator>

namespace tz::gfx
{

namespace
{

C2D_SpriteSheet sheet = nullptr;
C2D_SpriteSheet brandSheet = nullptr;

// the generated sprite indices, in the order of the Icon enum
constexpr size_t kIconIndex[] = {
    icons_theme_idx,
    icons_splash_idx,
    icons_badge_idx,
    icons_download_idx,
    icons_install_idx,
    icons_check_idx,
    icons_check_circle_idx,
    icons_music_idx,
    icons_music_only_idx,
    icons_no_music_idx,
    icons_shuffle_idx,
    icons_trash_idx,
    icons_eye_idx,
    icons_search_idx,
    icons_search_empty_idx,
    icons_qr_idx,
    icons_settings_idx,
    icons_info_idx,
    icons_refresh_idx,
    icons_restore_idx,
    icons_sort_idx,
    icons_globe_idx,
    icons_wifi_idx,
    icons_wifi_off_idx,
    icons_warning_idx,
    icons_error_idx,
    icons_update_idx,
    icons_save_idx,
    icons_power_idx,
    icons_screens_idx,
    icons_screen_top_idx,
    icons_screen_bottom_idx,
    icons_square_idx,
    icons_square_check_idx,
    icons_battery_idx,
    icons_battery_charging_idx,
    icons_images_idx,
    icons_keyboard_idx,
    icons_sd_card_idx,
};
static_assert(std::size(kIconIndex) == static_cast<size_t>(Icon::Count), "an icon is missing from kIconIndex");

// the icons also drawn at 48 pixels (LARGE_ICONS in tools/make_icons.py)
int largeIndex(Icon which)
{
    switch (which)
    {
    case Icon::Theme:
        return icons_theme_48_idx;
    case Icon::Splash:
        return icons_splash_48_idx;
    case Icon::Badge:
        return icons_badge_48_idx;
    case Icon::Error:
        return icons_error_48_idx;
    case Icon::Warning:
        return icons_warning_48_idx;
    case Icon::SearchEmpty:
        return icons_search_empty_48_idx;
    case Icon::WifiOff:
        return icons_wifi_off_48_idx;
    case Icon::Qr:
        return icons_qr_48_idx;
    case Icon::CheckCircle:
        return icons_check_circle_48_idx;
    case Icon::Update:
        return icons_update_48_idx;
    case Icon::Info:
        return icons_info_48_idx;
    case Icon::Trash:
        return icons_trash_48_idx;
    default:
        return -1;
    }
}

constexpr float kPi = 3.14159265f;

// A quarter circle, the quarter-th clockwise from 3 o'clock (y points down), as a fan of triangles, which don't
// overlap, so translucent colours stay even. The GPU doesn't smooth edges, so the fan stops half a pixel short of the
// edge and a ring fades the colour out over the pixel across it, as much as a pixel there is covered.
void corner(float cx, float cy, float radius, int quarter, Color color)
{
    color = faded(color);
    const int steps = std::clamp(static_cast<int>(std::ceil(radius / 2)), 3, 8);
    const float start = (kPi / 2) * static_cast<float>(quarter);
    const float inner = std::max(0.0f, radius - 0.5f);
    const float outer = radius + 0.5f;
    const Color clear = withAlpha(color, 0);
    float cos0 = std::cos(start);
    float sin0 = std::sin(start);
    for (int i = 1; i <= steps; ++i)
    {
        const float angle = start + (kPi / 2) * static_cast<float>(i) / static_cast<float>(steps);
        const float cos1 = std::cos(angle);
        const float sin1 = std::sin(angle);
        const float ix0 = cx + inner * cos0, iy0 = cy + inner * sin0;
        const float ix1 = cx + inner * cos1, iy1 = cy + inner * sin1;
        const float ox0 = cx + outer * cos0, oy0 = cy + outer * sin0;
        const float ox1 = cx + outer * cos1, oy1 = cy + outer * sin1;
        C2D_DrawTriangle(cx, cy, color, ix0, iy0, color, ix1, iy1, color, 0);
        C2D_DrawTriangle(ix0, iy0, color, ox0, oy0, clear, ox1, oy1, clear, 0);
        C2D_DrawTriangle(ix0, iy0, color, ox1, oy1, clear, ix1, iy1, color, 0);
        cos0 = cos1;
        sin0 = sin1;
    }
}

} // namespace

bool loadIcons()
{
    sheet = C2D_SpriteSheetLoad("romfs:/gfx/icons.t3x");
    brandSheet = C2D_SpriteSheetLoad("romfs:/gfx/brand.t3x");
    return sheet != nullptr && brandSheet != nullptr;
}

void freeIcons()
{
    if (sheet)
        C2D_SpriteSheetFree(sheet);
    if (brandSheet)
        C2D_SpriteSheetFree(brandSheet);
    sheet = nullptr;
    brandSheet = nullptr;
}

void brand(Brand which, float x, float y, float alpha)
{
    if (!brandSheet)
        return;
    const C2D_Image image = C2D_SpriteSheetGetImage(brandSheet, which == Brand::Logo ? brand_logo_idx : brand_icon_64_idx);
    alpha *= opacity();
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    C2D_DrawImageAt(image, x, y, 0, alpha < 1.0f ? &tint : nullptr, 1.0f, 1.0f);
}

float brandWidth(Brand which)
{
    if (!brandSheet)
        return 0;
    return C2D_SpriteSheetGetImage(brandSheet, which == Brand::Logo ? brand_logo_idx : brand_icon_64_idx)
        .subtex->width;
}

float brandHeight(Brand which)
{
    if (!brandSheet)
        return 0;
    return C2D_SpriteSheetGetImage(brandSheet, which == Brand::Logo ? brand_logo_idx : brand_icon_64_idx)
        .subtex->height;
}

void icon(Icon which, float x, float y, float size, Color color)
{
    if (!sheet || which >= Icon::Count)
        return;
    size_t index = kIconIndex[static_cast<size_t>(which)];
    if (size > metric::kIcon)
        if (const int large = largeIndex(which); large >= 0)
            index = static_cast<size_t>(large);
    const C2D_Image image = C2D_SpriteSheetGetImage(sheet, index);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, faded(color), 1.0f);
    const float scale = size / static_cast<float>(image.subtex->width);
    C2D_DrawImageAt(image, std::round(x), std::round(y), 0, &tint, scale, scale);
}

void rect(float x, float y, float w, float h, Color color)
{
    if (w > 0 && h > 0)
        C2D_DrawRectSolid(x, y, 0, w, h, faded(color));
}

void gradientV(float x, float y, float w, float h, Color top, Color bottom)
{
    top = faded(top);
    bottom = faded(bottom);
    C2D_DrawRectangle(x, y, 0, w, h, top, top, bottom, bottom);
}

void gradientH(float x, float y, float w, float h, Color left, Color right)
{
    left = faded(left);
    right = faded(right);
    C2D_DrawRectangle(x, y, 0, w, h, left, right, left, right);
}

void roundedRect(float x, float y, float w, float h, float radius, Color color)
{
    radius = std::min({radius, w / 2, h / 2});
    if (radius <= 0.5f)
    {
        rect(x, y, w, h, color);
        return;
    }
    rect(x + radius, y, w - 2 * radius, h, color);
    rect(x, y + radius, radius, h - 2 * radius, color);
    rect(x + w - radius, y + radius, radius, h - 2 * radius, color);
    corner(x + radius, y + radius, radius, 2, color);
    corner(x + w - radius, y + radius, radius, 3, color);
    corner(x + w - radius, y + h - radius, radius, 0, color);
    corner(x + radius, y + h - radius, radius, 1, color);
}

void circle(float cx, float cy, float radius, Color color)
{
    // four quarter fans, as the rounded corners are drawn: citro2d's own circles don't show in Azahar
    for (int quarter = 0; quarter < 4; ++quarter)
        corner(cx, cy, radius, quarter, color);
}

void line(float x0, float y0, float x1, float y1, float width, Color color)
{
    color = faded(color);
    C2D_DrawLine(x0, y0, color, x1, y1, color, width, 0);
}

void shadow(float x, float y, float w, float h, float size)
{
    const Color dark = color::kShadow;
    const Color clear = withAlpha(color::kShadow, 0);
    gradientV(x, y - size, w, size, clear, dark);
    gradientV(x, y + h, w, size, dark, clear);
    gradientH(x - size, y, size, h, clear, dark);
    gradientH(x + w, y, size, h, dark, clear);
}

void spinner(float cx, float cy, float radius, Color color, float shown)
{
    // a head and a fading tail of dots, going round; or as many dots as shown, from the top, still
    constexpr int kDots = 8;
    const bool whole = shown >= 1;
    const float turn = whole ? std::fmod(time() * 1.4f, 1.0f) : 0.75f;
    const int dots = whole ? kDots : static_cast<int>(std::ceil(std::max(0.0f, shown) * kDots));
    for (int i = 0; i < dots; ++i)
    {
        const float angle = 2 * kPi * (turn + static_cast<float>(i) / kDots);
        const float fade = whole ? static_cast<float>(i + 1) / kDots : 1.0f;
        circle(cx + radius * std::cos(angle), cy + radius * std::sin(angle), radius * 0.16f + radius * 0.08f * fade,
               withAlpha(color, fade));
    }
}

void progressBar(float x, float y, float w, float h, float fraction, Color track, Color fillColor)
{
    roundedRect(x, y, w, h, h / 2, track);
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (fraction > 0)
        roundedRect(x, y, std::max(h, w * fraction), h, h / 2, fillColor);
}

void fill(Color color)
{
    rect(0, 0, screenWidth(), metric::kHeight, color);
}

} // namespace tz::gfx
