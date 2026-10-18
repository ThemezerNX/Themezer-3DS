// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "core/motion.hpp"
#include "test.hpp"

#include <algorithm>
#include <cmath>

using namespace tz;

TEST(motion_spring_settles_without_going_past)
{
    // let go 60 pixels past the end
    float over = 60;
    float velocity = 0;
    for (int frame = 0; frame < 30; ++frame) // half a second
    {
        motion::springStep(over, velocity, 18, 1.0f / 60);
        CHECK(over >= 0);
    }
    CHECK(over < 0.5f);
    CHECK(std::fabs(velocity) < 10);
}

TEST(motion_spring_is_stable_in_long_frames)
{
    // a frame of a tenth of a second, the longest the app gives, is as calm as six short ones
    float over = 60;
    float velocity = 0;
    float shortOver = 60;
    float shortVelocity = 0;
    motion::springStep(over, velocity, 18, 0.1f);
    for (int i = 0; i < 6; ++i)
        motion::springStep(shortOver, shortVelocity, 18, 0.1f / 6);
    CHECK(std::fabs(over - shortOver) < 0.01f);
    CHECK(std::fabs(velocity - shortVelocity) < 0.1f);
}

TEST(motion_fling_into_the_end_bounces_back)
{
    // reaching the end at 1400 pixels a second: it goes on past it a little, then comes back to rest
    float over = 0;
    float velocity = 1400;
    float furthest = 0;
    for (int frame = 0; frame < 60; ++frame)
    {
        motion::springStep(over, velocity, 18, 1.0f / 60);
        furthest = std::max(furthest, over);
    }
    // v / (omega e) for a critically damped spring
    CHECK(furthest > 25 && furthest < 32);
    CHECK(over < 0.5f);
}

TEST(motion_rubber_never_goes_as_far_as_its_room)
{
    // dragging 2000 pixels past the end of a list 184 pixels high
    constexpr float kRoom = 184;
    float over = 0;
    for (int step = 0; step < 2000; ++step)
        over += motion::rubberGive(over, kRoom);
    CHECK(over < kRoom);
    CHECK(over > kRoom / 2);
    // it gives 0.55 at the end, and less further on
    CHECK(std::fabs(motion::rubberGive(0, kRoom) - 0.55f) < 1e-6f);
    CHECK(motion::rubberGive(-50, kRoom) == motion::rubberGive(50, kRoom));
    CHECK(motion::rubberGive(100, kRoom) < motion::rubberGive(50, kRoom));
}
