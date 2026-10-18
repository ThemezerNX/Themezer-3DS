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

TEST(motion_coast_bounces_off_an_end_however_hard_the_fling)
{
    // flung at 5000 pixels a second towards the bottom of a page that ends at 300, even in frames a tenth of a second
    // long: it goes on past the end by no more than 30 pixels, and comes to rest on it
    for (const float dt : {1.0f / 60, 0.1f})
    {
        float position = 250;
        float velocity = 5000;
        float furthest = 0;
        for (int frame = 0; frame < 120; ++frame)
        {
            motion::coast(position, velocity, 0, 300, 4, dt);
            furthest = std::max(furthest, position);
        }
        CHECK(furthest > 300 && furthest < 330);
        CHECK(position == 300);
        CHECK(velocity == 0);
    }
}

TEST(motion_coast_slows_a_fling_between_the_ends)
{
    float position = 0;
    float velocity = 600;
    for (int frame = 0; frame < 180; ++frame)
        motion::coast(position, velocity, 0, 10000, 4, 1.0f / 60);
    // about 600 / 4 pixels in all, as friction has it, a little more in steps of a frame
    CHECK(velocity == 0);
    CHECK(position > 140 && position < 160);
}

TEST(motion_coast_brings_a_pull_back)
{
    float position = -60;
    float velocity = 0;
    for (int frame = 0; frame < 60; ++frame)
        motion::coast(position, velocity, 0, 300, 4, 1.0f / 60);
    CHECK(position == 0);
    CHECK(velocity == 0);
}
