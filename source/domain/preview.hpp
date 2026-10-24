// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Preview pictures (preview.png): what a theme or splash looks like on the two screens.

#pragma once

#include "core/image.hpp"

#include <optional>
#include <string_view>

namespace tz::preview
{

struct Screens
{
    Image top;                   // 400x240
    std::optional<Image> bottom; // 320x240, when the picture shows the bottom screen too
};

// Screenshot-shaped pictures are cut into their two screens: 400x480 (the usual preview), 406 to 412 wide (theme
// editors keep their wider top texture), 412x482 (YATA+), and 432x528 (HOME Menu screenshots, with a 16 pixel
// border). Others (a badge set's sheet, a banner) are fitted into the top screen whole.
Screens split(const Image& picture);

// The ThumbHash of a picture (both screens, or an icon where there's no preview), drawn: a tiny picture, which drawn
// stretched is a soft blur of it. The backdrop behind the app's pages, as the Themezer website draws the ThumbHash of
// what's open behind its pages.
Image blurSource(const Image& picture);
// The two screens as one picture, the top one over the bottom one centred under it (see-through beside it), as a
// theme's collage has them and the backdrop spans them; the top one alone when there's no bottom one.
Image bothScreens(const Screens& screens);
// A ThumbHash as Themezer's API gives them (base64), drawn the same way. Empty when it isn't one.
Image thumbHashPicture(std::string_view base64);

} // namespace tz::preview
