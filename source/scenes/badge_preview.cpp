// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/badge_preview.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "core/image.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/frame.hpp"
#include "gfx/texture.hpp"
#include "sys/jobs.hpp"
#include "sys/log.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tz::scenes
{

using namespace gfx;

namespace
{

// The page: five columns of badges as wide as the bottom screen, which the top screen shows centred, so that a row
// goes on from the top screen onto the bottom one as the page scrolls.
constexpr int kColumns = 5;
constexpr int kBadge = 48; // as the lists show them
constexpr int kGap = 8;
constexpr float kPitch = kBadge + kGap;
constexpr float kLeft = (metric::kBottomWidth - (kColumns * kBadge + (kColumns - 1) * kGap)) / 2.0f;
constexpr float kMargin = 16; // above the first row and under the last
constexpr float kScrollSpeed = 320; // pixels a second, up or down held
constexpr float kFriction = 5;      // how fast a fling slows down

// The badges are kept in textures of 10 x 10, made as they fill: a thousand badges, the HOME Menu's most, take ten.
constexpr int kTextureSize = 512;
constexpr int kPerRow = kTextureSize / kBadge;
constexpr int kPerTexture = kPerRow * kPerRow;
// handed from the worker to the main thread so many at a time
constexpr size_t kBatch = 10;

class BadgePreview final : public ui::Scene
{
public:
    explicit BadgePreview(BadgePreviewSource source) : source_(std::move(source))
    {
        // all it can take, so that adding one doesn't move those the GPU is to draw from
        textures_.reserve((badges::kMaxBadges + kPerTexture - 1) / kPerTexture);
        load();
    }

    ~BadgePreview() override
    {
        job_.cancel();
        for (C3D_Tex& texture : textures_)
            C3D_TexDelete(&texture);
    }

    void update(ui::Input& input, float dt) override
    {
        hintsShown_ = std::max(0.0f, hintsShown_ - dt);
        hints_ = {{KEY_B, tr("Close")}};
        if (maxScroll() > 0)
            hints_.push_back({KEY_UP | KEY_DOWN, tr("Scroll")});
        if (source_.hasNeighbour && source_.hasNeighbour(-1))
            hints_.push_back({KEY_LEFT, tr("Previous")});
        if (source_.hasNeighbour && source_.hasNeighbour(1))
            hints_.push_back({KEY_RIGHT, tr("Next")});
        if (hintsShown_ > 0)
            hintBar_.update(input, hints_);
        if (input.pressed(KEY_B | KEY_Y | KEY_A) || (input.tap && hintsShown_ <= 0))
        {
            close();
            return;
        }
        // the next set with right or R, the one before with left or L; not on a diagonal while scrolling
        const bool scrolling = input.held & (KEY_UP | KEY_DOWN);
        const int step = scrolling                              ? 0
                         : input.repeated(KEY_RIGHT | KEY_R) ? 1
                         : input.repeated(KEY_LEFT | KEY_L)  ? -1
                                                             : 0;
        if (step != 0 && source_.hasNeighbour && source_.hasNeighbour(step))
        {
            source_ = source_.neighbour(step);
            load();
            hintsShown_ = 2.5f;
            return;
        }
        scroll(input, dt);
        if (input.touching || input.down)
            hintsShown_ = 2.5f;
    }

    void drawTop() override
    {
        drawPage(0, metric::kTopWidth);
        if (count_ == 0)
        {
            if (done_)
                drawNothingToShow(source_.set.name, full_ ? tr("Out of memory") : tr("has no badges to show"));
            else
                spinner(metric::kTopWidth / 2, metric::kHeight / 2, 14, color::kText);
        }
    }

    void drawBottom() override
    {
        drawPage(metric::kHeight, metric::kBottomWidth);
        // the hints for a moment, then the badges alone; a press or a touch brings them back
        hintBar_.draw(hints_, std::min(1.0f, hintsShown_ * 2));
    }

private:
    // The set's badges, read on a worker and handed over a few at a time, so that the first rows show at once.
    void load()
    {
        job_.cancel();
        count_ = 0;
        done_ = false;
        full_ = false;
        scroll_ = 0;
        velocity_ = 0;
        flinging_ = false;
        CancelToken token;
        job_ = JobGuard(token);
        // before the page's own jobs, as the preview someone is looking at goes first on its lane
        constexpr int kPriority = 10;
        jobs::run(
            Lane::Io, token,
            [this, set = source_.set](const CancelToken& t) {
            std::vector<Image> batch;
            int read = 0;
            const auto handOver = [&] {
                if (batch.empty())
                    return;
                jobs::post([this, t, badges = std::move(batch)]() mutable {
                    if (!t.cancelled())
                        add(std::move(badges));
                });
                batch.clear();
            };
            auto result = badges::eachBadge(
                App::get().sd(), set,
                [&](Image badge) {
                    // the main thread takes no batches while the app is in the background (the HOME Menu is open)
                    while (!aptIsActive() && !t.cancelled())
                        svcSleepThread(50'000'000);
                    batch.push_back(resize(badge, kBadge, kBadge));
                    if (batch.size() >= kBatch)
                        handOver();
                    return ++read < badges::kMaxBadges && !t.cancelled();
                },
                t);
            handOver();
            if (!result && result.error().code != Errc::Cancelled)
                log::writeError("badge preview", result.error());
            jobs::post([this, t] {
                if (!t.cancelled())
                    done_ = true;
            });
            },
            kPriority);
    }

    // Main thread: puts badges into the textures, a new one when the last is full.
    void add(std::vector<Image> badges)
    {
        for (const Image& badge : badges)
        {
            if (full_)
                return;
            const int texture = count_ / kPerTexture;
            if (texture == static_cast<int>(textures_.size()))
            {
                C3D_Tex added{};
                if (!C3D_TexInit(&added, kTextureSize, kTextureSize, GPU_RGBA8))
                {
                    // no memory for more: what's there stays
                    LOG_WARN("badge preview: no memory for more than %d badges", count_);
                    full_ = true;
                    job_.cancel();
                    done_ = true;
                    return;
                }
                C3D_TexSetFilter(&added, GPU_LINEAR, GPU_LINEAR);
                textures_.push_back(added);
            }
            const int slot = count_ % kPerTexture;
            const int y = (slot / kPerRow) * kBadge;
            C3D_Tex& into = textures_[static_cast<size_t>(texture)];
            tileRgba8(badge, static_cast<uint32_t*>(into.data), kTextureSize, kTextureSize, (slot % kPerRow) * kBadge, y);
            // the rows of tiles the badge is in, for the GPU to see
            constexpr size_t kTileRowBytes = (kTextureSize / 8) * 64 * sizeof(uint32_t);
            GSPGPU_FlushDataCache(static_cast<uint8_t*>(into.data) + (y / 8) * kTileRowBytes,
                                  (kBadge / 8) * kTileRowBytes);
            ++count_;
        }
    }

    float contentHeight() const
    {
        const int rows = (count_ + kColumns - 1) / kColumns;
        // and the hint bar's height under the last row, which it would cover
        return 2 * kMargin + rows * kPitch - kGap + metric::kHintBarHeight;
    }

    // how far the page goes past the two screens
    float maxScroll() const { return std::max(0.0f, contentHeight() - 2 * metric::kHeight); }

    void scroll(const ui::Input& input, float dt)
    {
        const bool down = input.held & KEY_DOWN;
        const bool up = input.held & KEY_UP;
        if (input.dragging)
        {
            scroll_ -= input.dy;
            velocity_ = 0;
            flinging_ = false;
        }
        else if (input.touchEnded)
        {
            velocity_ = -input.velocityY;
            flinging_ = true;
        }
        else if (down != up)
        {
            velocity_ = down ? kScrollSpeed : -kScrollSpeed;
            flinging_ = false;
        }
        else if (flinging_)
        {
            velocity_ *= std::exp(-kFriction * dt);
            if (std::abs(velocity_) < 10)
                velocity_ = 0;
        }
        else
        {
            // up or down let go: the page stops where it is
            velocity_ = 0;
        }
        scroll_ += velocity_ * dt;
        const float clamped = std::clamp(scroll_, 0.0f, maxScroll());
        if (clamped != scroll_)
            velocity_ = 0;
        scroll_ = clamped;
    }

    // One screen's part of the page: the top screen shows it from scroll_, the bottom one from a screen lower down.
    void drawPage(float screenTop, float screenWidth)
    {
        fill(color::kBackground);
        const float left = (screenWidth - metric::kBottomWidth) / 2 + kLeft;
        const float top = std::round(scroll_) + screenTop - kMargin;
        const int firstRow = std::max(0, static_cast<int>(std::floor(top / kPitch)));
        const int lastRow = static_cast<int>(std::floor((top + metric::kHeight) / kPitch));
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = 0; column < kColumns; ++column)
            {
                const int index = row * kColumns + column;
                if (index >= count_)
                    return;
                const int slot = index % kPerTexture;
                drawTexturePart(&textures_[static_cast<size_t>(index / kPerTexture)],
                                static_cast<float>((slot % kPerRow) * kBadge), static_cast<float>((slot / kPerRow) * kBadge),
                                kBadge, kBadge, left + column * kPitch, row * kPitch - top, kBadge, kBadge, 1.0f);
            }
        }
    }

    BadgePreviewSource source_;
    JobGuard job_;
    std::vector<C3D_Tex> textures_;
    int count_ = 0;     // badges in the textures
    bool done_ = false; // all read, or as many as fit
    bool full_ = false; // no memory for another texture
    float scroll_ = 0;
    float velocity_ = 0;
    bool flinging_ = false; // coasting after a drag, rather than moved by up or down
    float hintsShown_ = 2.5f;
    ui::HintBar hintBar_;
    std::vector<ui::Hint> hints_;
};

} // namespace

std::unique_ptr<ui::Scene> makeBadgePreview(BadgePreviewSource source)
{
    return std::make_unique<BadgePreview>(std::move(source));
}

} // namespace tz::scenes
