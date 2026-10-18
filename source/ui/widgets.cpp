// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "ui/widgets.hpp"

#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "sys/sfx.hpp"

#include <algorithm>
#include <cmath>

namespace tz::ui
{

using namespace gfx;

namespace
{

// The hints are keycaps: pills on a ledge they sink into when pressed, A the widest, in the accent.
constexpr float kHintGap = 4;
constexpr float kHintMargin = 4;
constexpr float kKeyHeight = 19;
constexpr float kKeyPadding = 6;  // either side of the button and label
constexpr float kPressTime = 0.11f; // how long a pressed key stays down at least, as long as it's held

// Where a button's hint goes, left to right as on a Switch: the others first, then SELECT (-), START (+), Y, X, B, A.
int hintRank(u32 key)
{
    switch (key)
    {
    case KEY_SELECT:
        return 1;
    case KEY_START:
        return 2;
    case KEY_Y:
        return 3;
    case KEY_X:
        return 4;
    case KEY_B:
        return 5;
    case KEY_A:
        return 6;
    default:
        return 0;
    }
}

bool isWordButton(u32 key)
{
    return key == KEY_START || key == KEY_SELECT;
}


// START and SELECT: a small pill with the word, as the buttons are printed
struct WordButton
{
    float textSize;
    float width;
    float height;
};

WordButton wordButton(u32 key, float size)
{
    const float textSize = size * 0.62f;
    return {textSize, std::round(textWidth(buttonGlyph(key), textSize) + 8), std::round(lineHeight(size) * 0.72f)};
}

} // namespace

const char* buttonGlyph(u32 key)
{
    switch (key)
    {
    case KEY_A:
        return glyph::kA;
    case KEY_B:
        return glyph::kB;
    case KEY_X:
        return glyph::kX;
    case KEY_Y:
        return glyph::kY;
    case KEY_L:
        return glyph::kL;
    case KEY_R:
        return glyph::kR;
    case KEY_START:
        return "START";
    case KEY_SELECT:
        return "SELECT";
    default:
        break;
    }
    const bool up = key & KEY_UP;
    const bool down = key & KEY_DOWN;
    const bool left = key & KEY_LEFT;
    const bool right = key & KEY_RIGHT;
    if ((up || down) && !left && !right)
        return up && down ? glyph::kDpadUpDown : (up ? glyph::kDpadUp : glyph::kDpadDown);
    if ((left || right) && !up && !down)
        return left && right ? glyph::kDpadLeftRight : (left ? glyph::kDpadLeft : glyph::kDpadRight);
    return glyph::kDpad;
}

float buttonWidth(u32 key, float size)
{
    return isWordButton(key) ? wordButton(key, size).width : textWidth(buttonGlyph(key), size);
}

float drawButton(u32 key, float x, float y, float size, Color color)
{
    if (!isWordButton(key))
        return drawText(buttonGlyph(key), x, y, size, color);
    const WordButton button = wordButton(key, size);
    const float top = std::round(textMiddle(y, size) - button.height / 2);
    x = std::round(x);
    roundedRect(x, top, button.width, button.height, button.height / 2, withAlpha(color, 0.9f));
    drawText(buttonGlyph(key), x + button.width / 2, textTop(top + button.height / 2, button.textSize),
             button.textSize, color::kOnAccent, Align::Center);
    return button.width;
}

float hintWidth(u32 key, std::string_view label, float labelSize)
{
    return buttonWidth(key, metric::kButtonGlyph) + 4 + textWidth(label, labelSize);
}

float drawHint(u32 key, std::string_view label, float x, float middle, Color buttonColor, Color labelColor,
               float labelSize)
{
    const float button = drawButton(key, x, textTop(middle, metric::kButtonGlyph), metric::kButtonGlyph, buttonColor);
    return button + 4 + drawText(label, x + button + 4, textTop(middle, labelSize), labelSize, labelColor);
}

float drawKey(float x, float y, float w, float h, Color face, bool down, float radius)
{
    if (radius < 0)
        radius = h / 2;
    if (down)
        y += kKeyLip;
    else if (opacity() >= 1) // it would show through a face that's fading
        roundedRect(x, y + kKeyLip, w, h, radius, mix(face, color::kBackground, 0.55f));
    roundedRect(x, y, w, h, radius, face);
    return y;
}

u32 HintBar::keyAt(float x, float y) const
{
    if (y < y_ || y >= y_ + height_)
        return 0;
    for (const Spot& spot : spots_)
    {
        if (x >= spot.x && x < spot.x + spot.width)
            return spot.key;
    }
    return 0;
}

void HintBar::update(Input& input, const std::vector<Hint>& hints)
{
    // a finger on a key holds it down; lifted there, without having moved off, it presses the key
    const u32 touched = input.touching && !input.dragging ? keyAt(input.x, input.y) : 0;
    touchedKey_ = touched == keyAt(input.startX, input.startY) ? touched : 0;
    if (input.tap)
    {
        if (const u32 key = keyAt(input.x, input.y); key && key == keyAt(input.startX, input.startY))
        {
            input.press(key);
            input.consumeTouch();
        }
    }
    // a press of a hint's button, on the bar or on the console, sinks its key for a moment, or while it's held
    held_ = input.held;
    for (const Hint& hint : hints)
    {
        if (input.down & hint.key)
        {
            pressedKey_ = hint.key;
            pressedAt_ = time();
        }
    }
}

void HintBar::draw(const std::vector<Hint>& hints, float alpha, Color accent)
{
    height_ = metric::kHintBarHeight;
    y_ = metric::kHeight - height_;
    spots_.clear();
    if (hints.empty() || alpha <= 0)
        return;
    const Layer layer(0, 0, alpha);
    rect(0, y_, metric::kBottomWidth, height_, withAlpha(color::kSurfaceSolid, 0.94f));

    // in that order, A last; smaller labels when they don't fit, and then the first ones go
    std::vector<const Hint*> order;
    order.reserve(hints.size());
    for (const Hint& hint : hints)
        order.push_back(&hint);
    std::stable_sort(order.begin(), order.end(),
                     [](const Hint* a, const Hint* b) { return hintRank(a->key) < hintRank(b->key); });
    float labelSize = metric::kTextNormal;
    auto keyWidth = [&](const Hint& hint) {
        return std::round(hintWidth(hint.key, hint.label, labelSize) + 2 * kKeyPadding);
    };
    auto total = [&](size_t first) {
        float sum = 2 * kHintMargin - kHintGap;
        for (size_t i = first; i < order.size(); ++i)
            sum += keyWidth(*order[i]) + kHintGap;
        return sum;
    };
    if (total(0) > metric::kBottomWidth)
        labelSize = metric::kTextSmall;
    size_t first = 0;
    while (first + 1 < order.size() && total(first) > metric::kBottomWidth)
        ++first;

    // right-aligned; A, when it's there, takes what's left of the bar
    const float spare = std::max(0.0f, metric::kBottomWidth - total(first));
    const bool wideA = order.back()->key == KEY_A;
    float x = kHintMargin + (wideA ? 0 : spare);
    const float top = std::round(y_ + (height_ - kKeyHeight - kKeyLip) / 2);
    const bool justPressed = time() - pressedAt_ < kPressTime;
    for (size_t i = first; i < order.size(); ++i)
    {
        const Hint& hint = *order[i];
        const bool primary = hint.key == KEY_A;
        const float width = keyWidth(hint) + (primary && wideA ? spare : 0);
        const bool down = (justPressed && hint.key == pressedKey_) || (held_ & hint.key) || hint.key == touchedKey_;
        const Color face = primary ? accent : color::kSurfaceHigh;
        const Color ink = primary ? color::kOnAccent : color::kText;
        const float keyTop = drawKey(x, top, width, kKeyHeight, face, down);
        const float content = hintWidth(hint.key, hint.label, labelSize);
        drawHint(hint.key, hint.label, x + std::round((width - content) / 2), keyTop + kKeyHeight / 2, ink, ink,
                 labelSize);
        // the whole height of the bar, and half the gaps
        spots_.push_back({x - kHintGap / 2, width + kHintGap, hint.key});
        x += width + kHintGap;
    }
    // as update saw them this frame: without an update (an overlay is over the scene) the keys come up
    held_ = 0;
    touchedKey_ = 0;
}

bool TabBar::update(Input& input)
{
    if (tabs_.empty())
        return false;
    const int count = static_cast<int>(tabs_.size());
    const int before = selected_;
    bool forward = true;
    // where a finger is on the bar, having stayed where it went down, for the symbols at the ends
    const auto onBar = [&](float x, float y) { return x >= left_ && x < right_ && y >= top_ && y < bottom_; };
    const bool touching =
        input.touching && !input.dragging && onBar(input.startX, input.startY) && onBar(input.x, input.y);
    const int touched = touching ? slotAt(input.x) : kNoSlot;
    bool tappedEnd = false;
    if (previousKey_ && input.pressed(previousKey_))
    {
        selected_ = (selected_ + count - 1) % count;
        forward = false;
    }
    else if (nextKey_ && input.pressed(nextKey_))
    {
        selected_ = (selected_ + 1) % count;
    }
    else if (input.tap && onBar(input.startX, input.startY) && onBar(input.x, input.y) && !slots_.empty())
    {
        // a tab, or the buttons' symbols at the ends, which go to the tab before or after
        const int slot = slotAt(input.x);
        if (slot == kBefore)
        {
            selected_ = (selected_ + count - 1) % count;
            forward = false;
        }
        else if (slot == kAfter)
        {
            selected_ = (selected_ + 1) % count;
        }
        else
        {
            selected_ = slot;
            forward = selected_ > before;
        }
        tappedEnd = slot < 0;
        input.consumeTouch();
    }
    previousPress_.update(touched == kBefore || (previousKey_ && (input.held & previousKey_)),
                          (previousKey_ && input.pressed(previousKey_)) || (tappedEnd && !forward));
    nextPress_.update(touched == kAfter || (nextKey_ && (input.held & nextKey_)),
                      (nextKey_ && input.pressed(nextKey_)) || (tappedEnd && forward));
    // the buttons switch tabs here, and do nothing else (the D-pad doesn't also page the list)
    input.down &= ~(previousKey_ | nextKey_);
    input.repeat &= ~(previousKey_ | nextKey_);
    if (selected_ == before)
        return false;
    forward_ = forward;
    sfx::play(sfx::Sound::Tab);
    return true;
}

int TabBar::slotAt(float x) const
{
    if (slots_.empty())
        return kNoSlot;
    if (x < slots_.front().x)
        return kBefore;
    if (x >= slots_.back().x + slots_.back().width)
        return kAfter;
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        if (x < slots_[i].x + slots_[i].width)
            return static_cast<int>(i);
    }
    return kAfter;
}

float TabBar::contentWidth(const Tab& tab, float size) const
{
    return tab.label.empty() ? metric::kIcon : textWidth(tab.label, size);
}

void TabBar::draw(float x, float y, float width, float height, float textSize)
{
    if (tabs_.empty())
        return;
    left_ = x;
    right_ = x + width;
    top_ = y;
    bottom_ = y + height;
    const float middle = y + height / 2;
    roundedRect(x + 3, y + 3, width - 6, height - 6, (height - 6) / 2, color::kSurface);

    // the buttons that switch the tabs, at the ends
    float left = x + 8;
    float right = x + width - 8;
    if (previousKey_ && nextKey_)
    {
        // pressed, a symbol goes down as far as the keys sink, and lights up
        const float glyphTop = textTop(middle, metric::kButtonGlyph);
        const bool previousDown = previousPress_.down();
        const bool nextDown = nextPress_.down();
        left += drawText(buttonGlyph(previousKey_), left, glyphTop + (previousDown ? kKeyLip : 0), metric::kButtonGlyph,
                         previousDown ? color::kText : color::kTextDim) +
                4;
        right -= textWidth(buttonGlyph(nextKey_), metric::kButtonGlyph);
        drawText(buttonGlyph(nextKey_), right, glyphTop + (nextDown ? kKeyLip : 0), metric::kButtonGlyph,
                 nextDown ? color::kText : color::kTextDim);
        right -= 4;
    }

    // each tab as wide as its name (or its icon), with what's left shared out evenly
    const size_t count = tabs_.size();
    float size = textSize;
    float names = 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        names = 0;
        for (const Tab& tab : tabs_)
            names += contentWidth(tab, size);
        if ((right - left - names) / static_cast<float>(count) >= 12 || size <= metric::kTextSmall)
            break;
        size = metric::kTextSmall;
    }
    const float padding = std::max(0.0f, (right - left - names) / static_cast<float>(2 * count));
    slots_.clear();
    float at = left;
    for (const Tab& tab : tabs_)
    {
        const float slotWidth = contentWidth(tab, size) + 2 * padding;
        slots_.push_back({at, slotWidth});
        at += slotWidth;
    }

    // the highlight slides to the selected tab
    const Slot& target = slots_[static_cast<size_t>(selected_)];
    if (highlightX_ < 0)
    {
        highlightX_ = target.x;
        highlightWidth_ = target.width;
    }
    highlightX_ += (target.x - highlightX_) * 0.3f;
    highlightWidth_ += (target.width - highlightWidth_) * 0.3f;
    if (std::fabs(target.x - highlightX_) < 0.5f)
    {
        highlightX_ = target.x;
        highlightWidth_ = target.width;
    }
    // flat, not a key: it shows where you are, there's nothing to press
    const float pillHeight = height - 10;
    roundedRect(std::round(highlightX_), y + 5, std::round(highlightWidth_), pillHeight, pillHeight / 2,
                withAlpha(tabs_[static_cast<size_t>(selected_)].accent.solid, 0.35f));

    const float labelTop = textTop(middle, size);
    for (size_t i = 0; i < count; ++i)
    {
        const Tab& tab = tabs_[i];
        const Color tint = static_cast<int>(i) == selected_ ? color::kText : color::kTextDim;
        const float centre = slots_[i].x + slots_[i].width / 2;
        if (tab.label.empty())
            icon(tab.icon, centre - metric::kIcon / 2, std::round(middle - metric::kIcon / 2), metric::kIcon, tint);
        else
            drawText(tab.label, centre, labelTop, size, tint, Align::Center);
    }
}

} // namespace tz::ui
