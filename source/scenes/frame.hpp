// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// What every page shares: the blurred backdrop and the top screen's status bar.

#pragma once

#include "gfx/draw.hpp"
#include "gfx/palette.hpp"
#include "gfx/texture.hpp"

#include <memory>
#include <string_view>

namespace tz::scenes
{

// A soft blur of the selected item's picture (its preview, else its icon) behind both screens, fading from one item to the next, as the Themezer
// website shows behind its pages. Without a picture, a dark wash of the page's accent.
class Backdrop
{
public:
    Backdrop();
    void set(std::shared_ptr<gfx::Picture> blur, gfx::Color accent);
    const std::shared_ptr<gfx::Picture>& picture() const { return current_.picture; }
    void tick(float dt);
    // Over the boot animation's chevrons, as the website draws it over its tile; see-through parts show them.
    void draw() const;

private:
    struct Layer
    {
        std::shared_ptr<gfx::Picture> picture;
        gfx::Color accent = gfx::color::kPrimary;
    };
    void drawLayer(const Layer& layer, float alpha) const;

    std::unique_ptr<gfx::Picture> pattern_; // the boot animation's chevrons, still (romfs/backdrop.png, tools/backdrop)
    Layer current_;
    Layer previous_;
    float fade_ = 1; // 0 shows the previous layer, 1 the current one
};

// The top screen's bar: Themezer's logo on the left, the time, Wi-Fi and battery on the right. Left out while a
// preview fills the top screen, since a theme's preview has the HOME Menu's own bar.
void drawStatusBar(float alpha = 1.0f);

// The lists' rows, the same on every page: a 36-pixel icon on the left, the name over a dimmer line, and what the
// item is (in use, has music...) at the right edge.
namespace row
{
constexpr float kIconSize = 36;
constexpr float kIconX = 14;
constexpr float kTextX = kIconX + kIconSize + 10;
constexpr float kRight = 12; // the margin at the right edge

// The selected row's key, as the list gives it: with a bar in the page's accent.
void highlight(float x, float y, float w, float h, bool down, gfx::Color accent);
// Where an icon of the row goes; a placeholder of the item's kind until the icon is there.
float iconTop(float y, float h);
void placeholder(float x, float y, float h, gfx::Icon kind);
// The two lines, centred in the row and cut to end at right; the name alone is centred.
void lines(std::string_view name, std::string_view detail, float x, float y, float h, float right,
           bool dimmed = false);
// At the right edge of the row, from right leftwards: a status (an icon and a word), or an icon alone. Both return
// where the next thing to their left ends.
float status(gfx::Icon icon, std::string_view text, gfx::Color color, float right, float y, float h);
float mark(gfx::Icon icon, gfx::Color color, float right, float y, float h);
} // namespace row

// The selected item on the top screen, in the same place on every page however long its description: its icon at the
// top left under the status bar, the name and author beside it, the description under them.
namespace details
{
constexpr float kLeft = 24;
constexpr float kTop = gfx::metric::kStatusBarHeight + 36; // room above for where it's from
constexpr float kDescriptionGap = 18; // from the icon to the description
// Where it's from, over its icon and name: the site online, the SD card on the SD card's pages.
void origin(gfx::Icon icon, std::string_view where);
} // namespace details

// The facts along the bottom of the top screen, as a list row has them: what the item is from the left (has music,
// how many badges), what's going on with it from the right (in use, picked for a shuffle, downloaded), so that
// neither moves the other.
class FactsLine
{
public:
    void left(gfx::Icon which, std::string_view text, gfx::Color tint);
    void right(gfx::Icon which, std::string_view text, gfx::Color tint);

private:
    float left_ = details::kLeft;
    float right_ = gfx::metric::kTopWidth - details::kLeft;
};

// An item the top screen has nothing to show of: its name, and why underneath, in the middle of the screen.
void drawNothingToShow(std::string_view name, std::string_view why);

// A page with nothing to show: a large icon, what's the matter, and what to do, centred between top and bottom.
void drawEmptyState(gfx::Icon icon, gfx::Color iconColor, std::string_view title, std::string_view message, float top,
                    float bottom);

} // namespace tz::scenes
