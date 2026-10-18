// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "ui/input.hpp"

#include <string>
#include <vector>

namespace tz::ui
{

// Keys on the screen (the hints, the mode button, a list's selected row) stand on a ledge this high, and sink into it
// while they're down.
constexpr float kKeyLip = 2;
// A key with its face at y, round at the ends (or with corners of radius), or sunk into the ledge while down. face must
// be opaque: the ledge is a darker copy of it underneath. Returns where the face's top is, for what's on it.
float drawKey(float x, float y, float w, float h, gfx::Color face, bool down, float radius = -1);

// Whether a key on the screen is down: while its button is held or a finger is on it, and for a moment at least after
// it's pressed, so that a quick press shows too. Update it in the frame's update, and ask once, in its draw: being held
// counts for that frame only, so a key comes up under an overlay that stops the updates.
class KeyPress
{
public:
    void update(bool held, bool pressed)
    {
        held_ = held;
        if (pressed)
            pressedAt_ = gfx::time();
    }
    bool down()
    {
        const bool held = held_;
        held_ = false;
        return held || gfx::time() - pressedAt_ < kTime;
    }

private:
    static constexpr float kTime = 0.11f;
    bool held_ = false;
    float pressedAt_ = -1;
};

// A button and what it does, along the bottom of the bottom screen. Tapping a hint presses its button.
struct Hint
{
    u32 key = 0;
    std::string label;
};

class HintBar
{
public:
    // Turns taps on the hints into their buttons. Call before handling input.
    void update(Input& input, const std::vector<Hint>& hints);
    // A's key in accent.
    void draw(const std::vector<Hint>& hints, float alpha = 1.0f, gfx::Color accent = gfx::color::kPrimary);

private:
    struct Spot
    {
        float x, width;
        u32 key;
    };
    u32 keyAt(float x, float y) const; // 0 for none
    std::vector<Spot> spots_;
    float y_ = 0;
    float height_ = 0;
    u32 pressedKey_ = 0; // the last of the hints' buttons pressed, and when
    float pressedAt_ = -1;
    u32 held_ = 0;
    u32 touchedKey_ = 0;
};

// The symbol for a button in the console's font, or "START"/"SELECT", which the font hasn't got.
const char* buttonGlyph(u32 key);
// How wide drawButton draws a button.
float buttonWidth(u32 key, float size);
// Draws a button as the hints do, with y at the top of the line. Returns its width.
float drawButton(u32 key, float x, float y, float size, gfx::Color color);
// A button and what it does, centred on middle: the button at metric::kButtonGlyph, the label at labelSize. Returns
// its width.
float hintWidth(u32 key, std::string_view label, float labelSize = gfx::metric::kTextNormal);
float drawHint(u32 key, std::string_view label, float x, float middle, gfx::Color buttonColor, gfx::Color labelColor,
               float labelSize = gfx::metric::kTextNormal);

// Tabs in a pill-shaped bar, as the website's switchers look: the selected one in its accent. Two buttons switch them,
// drawn at the bar's ends: L and R for the main tabs.
struct Tab
{
    std::string label; // empty for a tab that's only its icon
    gfx::Accent accent;
    gfx::Icon icon = gfx::Icon::Count;
};

class TabBar
{
public:
    explicit TabBar(u32 previousKey = KEY_L, u32 nextKey = KEY_R) : previousKey_(previousKey), nextKey_(nextKey) {}
    void setTabs(std::vector<Tab> tabs) { tabs_ = std::move(tabs); }
    int selected() const { return selected_; }
    void select(int index) { selected_ = index; }
    // The two buttons (which then do nothing else), or a tap on a tab. Returns whether the tab changed.
    bool update(Input& input);
    // Whether that change went to the next tab (or round from the last to the first), rather than back.
    bool movedForward() const { return forward_; }
    // The two buttons' symbols at the ends sink as they're pressed.
    // In the box from (x, y); the names at textSize, or smaller when they don't fit.
    void draw(float x, float y, float width, float height, float textSize);

private:
    struct Slot
    {
        float x, width;
    };
    float contentWidth(const Tab& tab, float size) const;
    // The tab at x, or the end before or after the tabs, from the last draw.
    static constexpr int kBefore = -1;
    static constexpr int kAfter = -2;
    static constexpr int kNoSlot = -3;
    int slotAt(float x) const;

    u32 previousKey_;
    u32 nextKey_;
    std::vector<Tab> tabs_;
    std::vector<Slot> slots_; // where the last draw put the tabs, for taps
    float left_ = 0;
    float right_ = 0;
    float top_ = 0;
    float bottom_ = 0;
    int selected_ = 0;
    bool forward_ = true;
    KeyPress previousPress_;
    KeyPress nextPress_;
    float highlightX_ = -1; // the highlight slides to the selected tab
    float highlightWidth_ = 0;
};

} // namespace tz::ui
