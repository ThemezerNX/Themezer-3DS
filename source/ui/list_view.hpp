// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "ui/entrance.hpp"
#include "ui/input.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace tz::ui
{

// A vertical list on the bottom screen: the D-pad moves the selection (left and right by a page), a touch selects a
// row and a second one opens it, and a drag scrolls with a fling. Dragged past an end, the list gives less and less and
// springs back when let go, and a fling that runs into an end bounces off it. Only the visible rows are drawn; the
// selected one is on a card raised on a ledge, which sinks into it under a finger, or as A opens it.
class ListView
{
public:
    enum class Event : uint8_t
    {
        None,
        Moved,     // the selection changed
        Activated, // A, or a tap on the selected row
        Pulled,    // with setPullToRefresh: pulled down kPullDistance past the top and let go
    };

    static constexpr float kPullDistance = 56;

    void setBounds(float x, float y, float width, float height);
    void setRowHeight(float height) { rowHeight_ = height; }
    // Keeps the selection in range; the list doesn't jump.
    void setCount(int count);
    int count() const { return count_; }
    // Room under the last row, for what's still loading; the last row selected shows it too. 0 for none.
    void setFooter(float height);

    int selected() const { return selected_; }
    void select(int index, bool animate = true);

    void setPullToRefresh(bool on) { pullToRefresh_ = on; }
    // Room kept open above the first row, for a refresh's spinner; 0 springs it shut.
    void holdTop(float gap);
    // How far the rows are down from the top of the list, pulled or held: where something over them can go.
    float pull() const { return std::max(0.0f, -scroll_); }

    Event update(Input& input, float dt);

    // drawRow(index, x, y, width, height, selected); drawFooter(x, y, width, height), when it's on screen;
    // drawHighlight(x, y, width, height, down) draws the selected row's key (ui::drawKey), gliding to it as it moves
    using RowPainter = std::function<void(int, float, float, float, float, bool)>;
    using FooterPainter = std::function<void(float, float, float, float)>;
    using HighlightPainter = std::function<void(float, float, float, float, bool)>;
    void draw(const RowPainter& drawRow, const FooterPainter& drawFooter = {},
              const HighlightPainter& drawHighlight = {}) const;

    // Started as the selection moves, from below when it went down: for what shows the selected row elsewhere.
    const Entrance& entrance() const { return entrance_; }

    // The rows that are at least partly on screen, for loading what they show first.
    std::pair<int, int> visibleRange() const;
    int rowsPerPage() const;
    bool settled() const;

private:
    float minScroll() const { return -topGap_; }
    float maxScroll() const;
    // How far the scroll is past an end: below 0 past the top, above it past the bottom.
    float overscroll() const;
    // After the rows or the room under them change: the target in range, the scroll as it is, past an end too, for
    // the spring to bring back, but no further than a row past the end. wasPast: it was past an end before.
    void fitScroll(bool wasPast);
    void keepSelectionVisible();
    int rowAt(float y) const; // -1 for none

    float x_ = 0;
    float y_ = 0;
    float width_ = 320;
    float height_ = 200;
    float rowHeight_ = 44;
    float footer_ = 0;
    int count_ = 0;
    int selected_ = 0;
    float scroll_ = 0;       // pixels from the top
    float target_ = 0;       // where the scroll is heading
    float velocity_ = 0;     // of a fling, pixels a second
    float highlight_ = 0;    // where the highlight is, from the top of the rows
    bool dragging_ = false;
    bool pressedInside_ = false;
    bool pullToRefresh_ = false;
    bool pulledFar_ = false; // as far as kPullDistance, in this drag
    float topGap_ = 0;
    float touchX_ = 0; // where the touch the list saw start started
    float touchY_ = 0;
    float pressedUntil_ = 0; // the selected row's card is sunk until then
    Entrance entrance_;
};

} // namespace tz::ui
