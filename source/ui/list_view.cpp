// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "ui/list_view.hpp"

#include "core/motion.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "sys/sfx.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace tz::ui
{

namespace
{
constexpr float kFollow = 16.0f;   // how fast the scroll catches up with its target
constexpr float kFriction = 4.0f;  // how fast a fling slows down
constexpr float kSpring = 18.0f;   // how fast the list comes back from past an end (the spring's angular frequency)
// A fling meets an end at most this fast, so that it bounces off by 30 pixels or so, however hard it was flung.
constexpr float kBounceSpeed = 1400.0f;
constexpr float kGlide = 24.0f;    // how fast the highlight goes to the selected row
constexpr float kScrollbarWidth = 3.0f;
// The selected row's key: centred in the row with its ledge under it, sinking into it while pressed and a moment at
// least, the row's content with it.
constexpr float kCardInsetX = 4;
constexpr float kCardInsetY = 3; // above the face, and under its ledge less the ledge
constexpr float kPressTime = 0.11f;
} // namespace

void ListView::setBounds(float x, float y, float width, float height)
{
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
}

void ListView::setCount(int count)
{
    const bool wasPast = overscroll() != 0;
    count_ = std::max(0, count);
    selected_ = std::clamp(selected_, 0, std::max(0, count_ - 1));
    // emptied for a reload: the next rows start at the top, not where the old ones were
    if (count_ == 0)
        highlight_ = scroll_ = target_ = velocity_ = 0;
    fitScroll(wasPast);
}

void ListView::setFooter(float height)
{
    height = std::max(0.0f, height);
    if (height == footer_)
        return;
    const bool wasPast = overscroll() != 0;
    footer_ = height;
    fitScroll(wasPast);
}

void ListView::fitScroll(bool wasPast)
{
    target_ = std::clamp(target_, minScroll(), maxScroll());
    scroll_ = std::min(scroll_, maxScroll() + rowHeight_);
    // rows came under a bounce at the end: the spring's speed back isn't a fling
    if (wasPast && overscroll() == 0)
        velocity_ = 0;
}

void ListView::holdTop(float gap)
{
    const bool wasPast = overscroll() != 0;
    // resting at the top, it rests at the gap's edge instead: the gap springs open, and shut again
    if (target_ <= minScroll() + 0.5f)
        target_ = -gap;
    topGap_ = gap;
    if (wasPast && overscroll() == 0)
        velocity_ = 0;
}

float ListView::overscroll() const
{
    if (scroll_ < minScroll())
        return scroll_ - minScroll();
    if (scroll_ > maxScroll())
        return scroll_ - maxScroll();
    return 0;
}

void ListView::select(int index, bool animate)
{
    if (count_ == 0)
        return;
    selected_ = std::clamp(index, 0, count_ - 1);
    keepSelectionVisible();
    if (!animate)
    {
        scroll_ = target_;
        highlight_ = selected_ * rowHeight_;
    }
}

float ListView::maxScroll() const
{
    return std::max(0.0f, count_ * rowHeight_ + footer_ - height_);
}

int ListView::rowsPerPage() const
{
    return std::max(1, static_cast<int>(height_ / rowHeight_));
}

void ListView::keepSelectionVisible()
{
    // a row of margin above and below the selection, where there is one; over the first row, the gap held open, and
    // under the last, the footer
    const float top = selected_ * rowHeight_;
    const float margin = height_ > rowHeight_ * 3 ? rowHeight_ * 0.6f : 0.0f;
    const float above = selected_ == 0 ? std::max(margin, topGap_) : margin;
    const float below = selected_ == count_ - 1 ? std::max(margin, footer_) : margin;
    if (top - above < target_)
        target_ = top - above;
    else if (top + rowHeight_ + below > target_ + height_)
        target_ = top + rowHeight_ + below - height_;
    target_ = std::clamp(target_, minScroll(), maxScroll());
    velocity_ = 0;
}

int ListView::rowAt(float y) const
{
    const int row = static_cast<int>(std::floor((y - y_ + scroll_) / rowHeight_));
    return row >= 0 && row < count_ ? row : -1;
}

bool ListView::settled() const
{
    return !dragging_ && std::fabs(target_ - scroll_) < 0.5f && velocity_ == 0;
}

std::pair<int, int> ListView::visibleRange() const
{
    if (count_ == 0)
        return {0, -1};
    const int first = std::clamp(static_cast<int>(scroll_ / rowHeight_), 0, count_ - 1);
    const int last = std::clamp(static_cast<int>((scroll_ + height_) / rowHeight_), 0, count_ - 1);
    return {first, last};
}

ListView::Event ListView::update(Input& input, float dt)
{
    Event event = Event::None;
    if (count_ > 0)
    {
        const int before = selected_;
        bool paged = false;
        if (input.repeated(KEY_UP))
        {
            // wrap around only on a fresh press, never while the key repeats
            if (selected_ > 0)
                --selected_;
            else if (input.pressed(KEY_UP))
                selected_ = count_ - 1;
        }
        else if (input.repeated(KEY_DOWN))
        {
            if (selected_ < count_ - 1)
                ++selected_;
            else if (input.pressed(KEY_DOWN))
                selected_ = 0;
        }
        else if (input.repeated(KEY_LEFT))
        {
            selected_ = std::max(0, selected_ - rowsPerPage());
            paged = true;
        }
        else if (input.repeated(KEY_RIGHT))
        {
            selected_ = std::min(count_ - 1, selected_ + rowsPerPage());
            paged = true;
        }
        if (selected_ != before)
        {
            keepSelectionVisible();
            event = Event::Moved;
            entrance_.start(selected_ > before ? 1 : -1);
            sfx::play(paged ? sfx::Sound::Page : sfx::Sound::Cursor);
        }
        else if (input.pressed(KEY_A))
        {
            event = Event::Activated;
            sfx::play(sfx::Sound::Confirm);
            pressedUntil_ = gfx::time() + kPressTime;
        }
    }

    // touch: drag to scroll, tap to select, tap the selected row to open it
    const bool inside = input.x >= x_ && input.x < x_ + width_ && input.y >= y_ && input.y < y_ + height_;
    // a touch the list didn't see end, or start (a scene was over it), leaves no drag behind
    if (input.touchStarted || (!input.touching && !input.touchEnded) ||
        (input.touching && (input.startX != touchX_ || input.startY != touchY_)))
    {
        dragging_ = pulledFar_ = false;
        pressedInside_ = input.touchStarted && inside;
        touchX_ = input.startX;
        touchY_ = input.startY;
    }
    // a finger on the selected row holds it down, until it lifts or starts a drag
    if (pressedInside_ && input.touching && !input.dragging && inside && count_ > 0 && rowAt(input.y) == selected_)
        pressedUntil_ = gfx::time() + kPressTime;
    else if (pressedInside_ && input.dragging)
        pressedUntil_ = 0;
    if (pressedInside_ && input.dragging)
    {
        dragging_ = true;
        // past an end, the list follows the finger less and less, and goes no further than it's high
        const float over = overscroll();
        scroll_ -= input.dy * (over != 0 ? motion::rubberGive(over, height_) : 1.0f);
        target_ = scroll_;
        velocity_ = 0;
        if (pullToRefresh_)
        {
            // far enough to refresh: a tick to say so
            const bool far = pull() >= kPullDistance;
            if (far && !pulledFar_)
                sfx::play(sfx::Sound::Cursor);
            pulledFar_ = far;
        }
    }
    if (input.touchEnded && pressedInside_)
    {
        if (dragging_)
        {
            velocity_ = -input.velocityY;
            if (pulledFar_ && event == Event::None)
            {
                event = Event::Pulled;
                sfx::play(sfx::Sound::Page);
            }
            pulledFar_ = false;
        }
        else if (input.tap && inside && count_ > 0)
        {
            if (const int row = rowAt(input.y); row >= 0)
            {
                if (row == selected_)
                {
                    event = Event::Activated;
                    sfx::play(sfx::Sound::Confirm);
                }
                else
                {
                    entrance_.start(row > selected_ ? 1 : -1);
                    selected_ = row;
                    keepSelectionVisible();
                    event = Event::Moved;
                    sfx::play(sfx::Sound::Cursor);
                }
                input.consumeTouch();
            }
        }
        dragging_ = false;
        pressedInside_ = false;
    }

    if (!dragging_)
    {
        const float over = overscroll();
        target_ = std::clamp(target_, minScroll(), maxScroll());
        // past an end and going back to it (not to a row the D-pad chose): a spring, which a fling bounces off
        if (over != 0 && std::fabs(target_ - (scroll_ - over)) < 0.5f)
        {
            float left = over;
            velocity_ = std::clamp(velocity_, -kBounceSpeed, kBounceSpeed);
            motion::springStep(left, velocity_, kSpring, dt);
            if (std::fabs(left) < 0.5f && std::fabs(velocity_) < 20.0f)
            {
                left = 0;
                velocity_ = 0;
            }
            scroll_ += left - over;
        }
        else if (over == 0 && velocity_ != 0)
        {
            // a fling, slowing down; one that reaches an end meets the spring there, no faster than kBounceSpeed
            const float next = scroll_ + velocity_ * dt;
            const float end = next > maxScroll() ? maxScroll() : (next < minScroll() ? minScroll() : next);
            if (end != next)
            {
                const float rest = dt * (next - end) / (next - scroll_);
                float past = 0;
                velocity_ = std::clamp(velocity_, -kBounceSpeed, kBounceSpeed);
                motion::springStep(past, velocity_, kSpring, rest);
                scroll_ = end + past;
            }
            else
            {
                scroll_ = next;
                velocity_ *= std::exp(-kFriction * dt);
                if (std::fabs(velocity_) < 20.0f)
                    velocity_ = 0;
            }
            target_ = std::clamp(scroll_, minScroll(), maxScroll());
        }
        else
        {
            velocity_ = 0;
            scroll_ += (target_ - scroll_) * std::min(1.0f, dt * kFollow);
        }
    }
    // to a row further than the list is tall, at once: across the whole list it would only be a blur
    const float goal = selected_ * rowHeight_;
    if (std::fabs(goal - highlight_) > height_)
        highlight_ = goal;
    highlight_ += (goal - highlight_) * std::min(1.0f, dt * kGlide);
    return event;
}

void ListView::draw(const RowPainter& drawRow, const FooterPainter& drawFooter,
                    const HighlightPainter& drawHighlight) const
{
    if (count_ == 0)
        return;
    gfx::pushClip(x_, y_, width_, height_);
    const bool sunk = gfx::time() < pressedUntil_;
    if (drawHighlight)
    {
        drawHighlight(x_ + kCardInsetX, std::round(y_ + highlight_ - scroll_) + kCardInsetY, width_ - 2 * kCardInsetX,
                      rowHeight_ - 2 * kCardInsetY, sunk);
    }
    const auto [first, last] = visibleRange();
    for (int i = first; i <= last; ++i)
    {
        const float shift = i == selected_ && sunk ? kKeyLip : 0;
        drawRow(i, x_, std::round(y_ + i * rowHeight_ - scroll_) + shift, width_, rowHeight_, i == selected_);
    }
    const float footerTop = y_ + count_ * rowHeight_ - scroll_;
    if (footer_ > 0 && drawFooter && footerTop < y_ + height_)
        drawFooter(x_, footerTop, width_, footer_);

    // a thin scrollbar when the rows don't fit
    const float total = count_ * rowHeight_ + footer_;
    if (total > height_)
    {
        const float barHeight = std::max(16.0f, height_ * height_ / total);
        const float barY = y_ + (height_ - barHeight) * std::clamp(scroll_ / maxScroll(), 0.0f, 1.0f);
        gfx::roundedRect(x_ + width_ - kScrollbarWidth - 1, barY, kScrollbarWidth, barHeight, kScrollbarWidth / 2,
                         gfx::rgba(255, 255, 255, 60));
    }
    gfx::popClip();
}

} // namespace tz::ui
