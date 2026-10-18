// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "gfx/renderer.hpp"

#include <algorithm>

namespace tz::ui
{

// What's just come on screen fades in and eases into place from a little to one side. Of starts close together, as a
// held button makes them, only the first plays: what comes after it shows as it is.
class Entrance
{
public:
    // From side -1 (the left, or above), 1 (the right, or below), or 0, fading in where it is.
    void start(int side)
    {
        const float now = gfx::time();
        if (now - last_ > kRun)
        {
            started_ = now;
            side_ = side;
        }
        last_ = now;
    }

    // Draws what follows, while it lives, as far as the entrance has got: up to distance pixels off, along x or y.
    gfx::Layer layer(float distance, bool vertical = false) const
    {
        const float t = std::clamp((gfx::time() - started_) / kDuration, 0.0f, 1.0f);
        const float eased = 1 - (1 - t) * (1 - t) * (1 - t);
        const float offset = static_cast<float>(side_) * distance * (1 - eased);
        return gfx::Layer(vertical ? 0 : offset, vertical ? offset : 0, eased);
    }

private:
    static constexpr float kDuration = 0.22f;
    static constexpr float kRun = 0.15f;
    float started_ = -1;
    float last_ = -1;
    int side_ = 0;
};

} // namespace tz::ui
