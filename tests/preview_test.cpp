// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/preview.hpp"
#include "test.hpp"

#include <cstdlib>

using namespace tz;

TEST(blur_source_is_a_thumbhash_of_the_top_screen)
{
    // red on the left, blue on the right, drawn 32 wide in the screen's shape
    Image top(400, 240);
    for (int y = 0; y < top.height; ++y)
    {
        for (int x = 0; x < top.width; ++x)
        {
            uint8_t* p = top.at(x, y);
            p[0] = x < 200 ? 230 : 20;
            p[1] = 20;
            p[2] = x < 200 ? 20 : 230;
            p[3] = 255;
        }
    }
    const Image blur = preview::blurSource(top);
    CHECK(blur.width == 32 && blur.height == 18);
    const uint8_t* left = blur.at(2, 9);
    const uint8_t* right = blur.at(29, 9);
    CHECK(left[0] > right[0] + 80 && right[2] > left[2] + 80);
    CHECK(left[3] == 255 && right[3] == 255);
}

TEST(both_screens_stack_the_bottom_one_centred)
{
    preview::Screens screens;
    screens.top = Image(400, 240);
    screens.bottom = Image(320, 240);
    for (size_t i = 0; i < screens.bottom->pixels.size(); ++i)
        screens.bottom->pixels[i] = 255;
    const Image both = preview::bothScreens(screens);
    CHECK(both.width == 400 && both.height == 480);
    CHECK(both.at(39, 300)[3] == 0 && both.at(40, 300)[3] == 255 && both.at(359, 479)[3] == 255);
    CHECK(both.at(360, 479)[3] == 0 && both.at(200, 239)[3] == 0);
    screens.bottom.reset();
    CHECK(preview::bothScreens(screens).height == 240);
}

TEST(blur_source_of_an_icon_is_square)
{
    Image icon(48, 48);
    for (size_t i = 0; i < icon.pixels.size(); i += 4)
    {
        icon.pixels[i] = 40;
        icon.pixels[i + 1] = 200;
        icon.pixels[i + 2] = 90;
        icon.pixels[i + 3] = 255;
    }
    const Image blur = preview::blurSource(icon);
    CHECK(blur.width == 32 && blur.height == 32);
    CHECK(blur.at(16, 16)[1] > 180 && blur.at(16, 16)[0] < 60);
}

TEST(thumbhash_picture_reads_the_sites_base64)
{
    // evanw/thumbhash's example of a portrait picture
    const Image picture = preview::thumbHashPicture("1QcSHQRnh493V4dIh4eXh1h4kJUI");
    CHECK(picture.height == 32 && picture.width > 0 && picture.width < 32);
    CHECK(preview::thumbHashPicture("").empty());
    CHECK(preview::thumbHashPicture("not base64!").empty());
    // short hashes are read no further than they go; longer ones than a ThumbHash can be aren't one
    CHECK(!preview::thumbHashPicture("1QcSHQQ=").empty());
    CHECK(preview::thumbHashPicture(std::string(60, 'A')).empty());
}
