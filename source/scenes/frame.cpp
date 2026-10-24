// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/frame.hpp"

#include "app/app.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tz::scenes
{

using namespace gfx;

namespace
{
// as the website draws its page background: its tile, then the picture; stronger than the website's half, as the
// 3DS's screens show dark colours much duller than a computer's
constexpr float kPictureOpacity = 0.75f;
} // namespace

Backdrop::Backdrop()
{
    if (FILE* file = std::fopen("romfs:/backdrop.png", "rb"))
    {
        Bytes png(64 * 1024);
        png.resize(std::fread(png.data(), 1, png.size(), file));
        std::fclose(file);
        if (auto image = decodePng(png))
            pattern_ = Picture::upload(tile(*image, true));
    }
}

void Backdrop::set(std::shared_ptr<Picture> blur, Color accent)
{
    if (blur == current_.picture && accent == current_.accent)
        return;
    previous_ = current_;
    current_ = Layer{std::move(blur), accent};
    fade_ = 0;
}

void Backdrop::tick(float dt)
{
    fade_ = std::min(1.0f, fade_ + dt * 3.5f);
    if (fade_ >= 1)
        previous_ = {};
}

void Backdrop::drawLayer(const Layer& layer, float alpha) const
{
    const float width = screenWidth();
    if (layer.picture)
    {
        // covering both screens in its own shape, centred, as the website covers its page: the top screen over the
        // bottom one, centred under it, as a theme's collage has them
        const Picture& picture = *layer.picture;
        const float scale = std::max(metric::kTopWidth / static_cast<float>(picture.width()),
                                     2 * metric::kHeight / static_cast<float>(picture.height()));
        const float w = picture.width() * scale;
        const float h = picture.height() * scale;
        const float top = metric::kHeight - h / 2 - (width < metric::kTopWidth ? metric::kHeight : 0);
        picture.draw((width - w) / 2, top, w, h, alpha * kPictureOpacity);
    }
    else
    {
        gradientV(0, 0, width, metric::kHeight, withAlpha(layer.accent, 0.22f * alpha), withAlpha(layer.accent, 0));
    }
}

void Backdrop::draw() const
{
    fill(color::kBackground);
    // its rows repeat every screen's height, so it carries on from the top screen into the bottom one
    if (pattern_)
        pattern_->draw(-(metric::kTopWidth - screenWidth()) / 2, 0, metric::kTopWidth, metric::kHeight);
    // one fading out as the other fades in, so that the picture's opacity holds
    if (fade_ < 1)
        drawLayer(previous_, 1 - fade_);
    drawLayer(current_, fade_);
}

void drawStatusBar(float alpha)
{
    const float height = metric::kStatusBarHeight;
    const float middle = height / 2;
    gradientV(0, 0, metric::kTopWidth, height + 8, withAlpha(rgba(12, 10, 9, 190), alpha), rgba(12, 10, 9, 0));
    brand(Brand::Logo, 8, std::round(middle - brandHeight(Brand::Logo) / 2), alpha);

    const App::Status& status = App::get().status();
    const float iconTop = std::round(middle - metric::kIcon / 2);
    float x = metric::kTopWidth - 8;

    // battery: Lucide's outline, filled to the charge level
    x -= metric::kIcon;
    const Color text = withAlpha(color::kText, alpha);
    if (status.charging)
    {
        icon(Icon::BatteryCharging, x, iconTop, metric::kIcon, text);
    }
    else
    {
        icon(Icon::Battery, x, iconTop, metric::kIcon, text);
        // the outline's inside is from (4.5, 9.5) of its 24-unit grid, 11 by 5
        const float unit = metric::kIcon / 24.0f;
        const float level = static_cast<float>(status.battery) / 5.0f;
        const Color charge = status.battery <= 1 ? withAlpha(color::kDanger, alpha) : text;
        rect(x + 4.5f * unit, iconTop + 9.5f * unit, std::round(11.0f * unit * level), 5.0f * unit, charge);
    }

    x -= 6 + metric::kIcon;
    icon(status.online ? Icon::Wifi : Icon::WifiOff, x, iconTop, metric::kIcon,
         status.online ? text : withAlpha(color::kTextFaint, alpha));

    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", status.hour, status.minute);
    drawText(clock, x - 6, textTop(middle, metric::kTextSmall), metric::kTextSmall, text, Align::Right);
}

namespace row
{

void highlight(float x, float y, float w, float h, bool down, Color accent)
{
    const float top = ui::drawKey(x, y, w, h, color::kSurfaceRaised, down, metric::kRadius);
    roundedRect(x + 2, top + 10, 3, h - 20, 1.5f, accent);
}

float iconTop(float y, float h)
{
    return std::round(y + (h - kIconSize) / 2);
}

void placeholder(float x, float y, float h, Icon kind)
{
    const float top = iconTop(y, h);
    roundedRect(x + kIconX, top, kIconSize, kIconSize, 6, color::kSurfaceRaised);
    icon(kind, x + kIconX + (kIconSize - metric::kIcon) / 2, top + (kIconSize - metric::kIcon) / 2, metric::kIcon,
         color::kTextFaint);
}

void lines(std::string_view name, std::string_view detail, float x, float y, float h, float right, bool dimmed)
{
    const float width = right - (x + kTextX);
    const float middle = y + h / 2;
    if (detail.empty())
    {
        drawText(name, x + kTextX, textTop(middle, metric::kTextNormal), metric::kTextNormal,
                 dimmed ? color::kTextFaint : color::kText, Align::Left, width);
        return;
    }
    drawText(name, x + kTextX, textTop(middle - 7.5f, metric::kTextNormal), metric::kTextNormal,
             dimmed ? color::kTextFaint : color::kText, Align::Left, width);
    drawText(detail, x + kTextX, textTop(middle + 8, metric::kTextSmall), metric::kTextSmall,
             dimmed ? color::kTextFaint : color::kTextDim, Align::Left, width);
}

float status(Icon which, std::string_view text, Color tint, float right, float y, float h)
{
    const float middle = y + h / 2;
    const float width = textWidth(text, metric::kTextSmall);
    right -= width;
    drawText(text, right, textTop(middle, metric::kTextSmall), metric::kTextSmall, tint);
    right -= 3 + metric::kIcon;
    icon(which, right, std::round(middle - metric::kIcon / 2), metric::kIcon, tint);
    return right - 8;
}

float mark(Icon which, Color tint, float right, float y, float h)
{
    right -= metric::kIcon;
    icon(which, right, std::round(y + h / 2 - metric::kIcon / 2), metric::kIcon, tint);
    return right - 8;
}

} // namespace row

namespace
{
constexpr float kFactsMiddle = metric::kHeight - 22;
constexpr float kFactGap = 16;
constexpr float kFactIconGap = 4;
} // namespace

void FactsLine::left(Icon which, std::string_view text, Color tint)
{
    icon(which, left_, std::round(kFactsMiddle - metric::kIcon / 2), metric::kIcon, tint);
    left_ += metric::kIcon + kFactIconGap;
    left_ += drawText(text, left_, textTop(kFactsMiddle, metric::kTextSmall), metric::kTextSmall, tint) + kFactGap;
}

void FactsLine::right(Icon which, std::string_view text, Color tint)
{
    const float x = std::round(right_ - textWidth(text, metric::kTextSmall));
    drawText(text, x, textTop(kFactsMiddle, metric::kTextSmall), metric::kTextSmall, tint);
    const float iconX = x - kFactIconGap - metric::kIcon;
    icon(which, iconX, std::round(kFactsMiddle - metric::kIcon / 2), metric::kIcon, tint);
    right_ = iconX - kFactGap;
}

namespace details
{

void origin(Icon which, std::string_view where)
{
    const float middle = kTop - 16;
    icon(which, kLeft, std::round(middle - metric::kIcon / 2), metric::kIcon, color::kTextDim);
    drawText(where, kLeft + metric::kIcon + 4, textTop(middle, metric::kTextSmall), metric::kTextSmall,
             color::kTextDim);
}

} // namespace details

void drawNothingToShow(std::string_view name, std::string_view why)
{
    const float x = metric::kTopWidth / 2;
    const float middle = metric::kHeight / 2;
    const float width = metric::kTopWidth - 2 * details::kLeft;
    if (!name.empty())
        drawText(name, x, textTop(middle - 12, metric::kTextTitle), metric::kTextTitle, color::kText, Align::Center,
                 width);
    drawText(why, x, textTop(name.empty() ? middle : middle + 14, metric::kTextNormal), metric::kTextNormal,
             color::kTextDim, Align::Center, width);
}

void drawEmptyState(Icon which, Color iconColor, std::string_view title, std::string_view message, float top,
                    float bottom)
{
    const float width = metric::kBottomWidth;
    const auto lines = wrap(message, width - 48, metric::kTextSmall, 4);
    const float messageHeight = static_cast<float>(lines.size()) * lineHeight(metric::kTextSmall);
    const float height = metric::kIconLarge + 12 + lineHeight(metric::kTextNormal) + 6 + messageHeight;
    float y = std::round((top + bottom - height) / 2);
    icon(which, (width - metric::kIconLarge) / 2, y, metric::kIconLarge, iconColor);
    y += metric::kIconLarge + 12;
    drawText(title, width / 2, y, metric::kTextNormal, color::kText, Align::Center, width - 32);
    y += lineHeight(metric::kTextNormal) + 6;
    drawWrapped(message, 24, y, width - 48, metric::kTextSmall, color::kTextDim, 4, Align::Center);
}

} // namespace tz::scenes
