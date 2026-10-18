// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/qr_view.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "core/image.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "gfx/texture.hpp"
#include "scenes/frame.hpp"
#include "ui/widgets.hpp"

namespace tz::scenes
{

using namespace gfx;

namespace
{

// The tab goes on screen, and its tab bar moves, before anything about the camera starts. A camera turned straight
// off again isn't turned on.
constexpr u64 kOpenDelayMs = 100;

} // namespace

QrView::QrView()
{
    textureReady_ = C3D_TexInit(&texture_, 512, 256, GPU_RGB565);
    if (textureReady_)
        C3D_TexSetFilter(&texture_, GPU_LINEAR, GPU_LINEAR);
}

QrView::~QrView()
{
    scanner_.reset();
    closing_.reset();
    if (textureReady_)
        C3D_TexDelete(&texture_);
}

void QrView::start()
{
    if (scanner_ || starting_)
        return;
    scanned_ = false;
    hasFrame_ = false;
    problem_.reset();
    starting_ = true;
    openAt_ = osGetTime() + kOpenDelayMs;
}

void QrView::open()
{
    auto scanner = QrScanner::start();
    if (scanner)
        scanner_ = std::move(*scanner);
    else
        showProblem(scanner.error());
}

void QrView::showProblem(const Error& error)
{
    if (error.code == Errc::NotAllowed && App::get().homebrew())
        problem_ = ErrorInfo{tr("The camera isn't available"),
                             tr("Started this way, the app can't use the camera. Install the CIA version to scan QR "
                                "codes.")};
    else if (error.code == Errc::Busy)
        problem_ = ErrorInfo{tr("The camera is busy"), tr("Something else is using the camera. Try again in a moment.")};
    else
        problem_ = describe(error);
}

void QrView::stop()
{
    starting_ = false;
    hasFrame_ = false;
    if (!scanner_)
        return;
    // off on its own threads, as that takes a while: tick() lets it go then
    scanner_->stop();
    closing_ = std::move(scanner_);
}

void QrView::tick()
{
    if (closing_ && closing_->stopped())
        closing_.reset();
}

std::optional<std::string> QrView::update()
{
    if (starting_)
    {
        if (!closing_ && osGetTime() >= openAt_)
        {
            starting_ = false;
            open();
        }
        return std::nullopt;
    }
    if (!scanner_)
        return std::nullopt;
    if (auto failure = scanner_->failure())
    {
        showProblem(*failure);
        stop();
        return std::nullopt;
    }
    if (textureReady_ && scanner_->latestFrame(frame_))
    {
        // the middle of the picture, in the top screen's shape, scaled down to it (640 to 400 pixels wide) as it goes
        // into the GPU's tiles
        constexpr int kShownWidth = static_cast<int>(metric::kTopWidth);
        constexpr int kShownHeight = static_cast<int>(metric::kHeight);
        constexpr int kTop = (QrScanner::kHeight - kShownHeight * QrScanner::kWidth / kShownWidth) / 2;
        auto* texels = static_cast<uint16_t*>(texture_.data);
        for (int y = 0; y < kShownHeight; ++y)
        {
            const uint16_t* row =
                frame_.data() + static_cast<size_t>(kTop + y * QrScanner::kWidth / kShownWidth) * QrScanner::kWidth;
            for (int x = 0; x < kShownWidth; ++x)
                texels[tiledIndex(x, y, 512)] = row[x * QrScanner::kWidth / kShownWidth];
        }
        C3D_TexFlush(&texture_);
        hasFrame_ = true;
    }
    if (auto text = scanner_->result())
    {
        scanned_ = true;
        stop(); // the camera stops at once
        return text;
    }
    return std::nullopt;
}

void QrView::drawTop()
{
    if (!showsPicture())
    {
        if (scanner_ || starting_)
            spinner(metric::kTopWidth / 2, metric::kHeight / 2, 14, color::kText);
        return;
    }
    drawTexturePart(&texture_, 0, 0, metric::kTopWidth, metric::kHeight, 0, 0, metric::kTopWidth, metric::kHeight,
                    1.0f);

    // corners to aim with
    const float size = 150;
    const float x = (metric::kTopWidth - size) / 2;
    const float y = (metric::kHeight - size) / 2;
    const Color edge = withAlpha(color::kPrimaryLight, 0.9f);
    const float l = 24;
    const float t = 3;
    rect(x, y, l, t, edge);
    rect(x, y, t, l, edge);
    rect(x + size - l, y, l, t, edge);
    rect(x + size - t, y, t, l, edge);
    rect(x, y + size - t, l, t, edge);
    rect(x, y + size - l, t, l, edge);
    rect(x + size - l, y + size - t, l, t, edge);
    rect(x + size - t, y + size - l, t, l, edge);
}

bool QrView::idFieldTapped(const ui::Input& input)
{
    const auto inside = [&](float x, float y) {
        return x >= fieldX_ && x < fieldX_ + fieldWidth_ && y >= fieldY_ && y < fieldY_ + fieldHeight_;
    };
    fieldTouched_ = input.touching && !input.dragging && inside(input.x, input.y) && inside(input.startX, input.startY);
    return input.tap && inside(input.x, input.y);
}

void QrView::drawBottom(float top, float bottom)
{
    const float width = metric::kBottomWidth;
    constexpr float kMargin = 16;

    // the ID field along the bottom, under an "or"
    fieldHeight_ = 36;
    fieldX_ = kMargin;
    fieldWidth_ = width - 2 * kMargin;
    fieldY_ = std::round(bottom - 12 - fieldHeight_);
    const float orMiddle = fieldY_ - 13;

    // what the camera is doing, above
    std::string title;
    std::string message;
    Color tint = color::kPrimaryLight;
    if (problem_)
    {
        title = problem_->title;
        message = problem_->message;
        tint = color::kTextDim;
    }
    else if (scanned_)
    {
        title = tr("Scanned");
        message = tr("Press A to scan another QR code.");
    }
    else
    {
        title = tr("Scan a QR code");
        message = tr("For example from themezer.net or themeplaza.art");
    }
    const auto lines = wrap(message, width - 2 * kMargin, metric::kTextSmall, 3);
    const float titleHeight = lineHeight(metric::kTextNormal);
    const float blockHeight = titleHeight + 4 + static_cast<float>(lines.size()) * lineHeight(metric::kTextSmall);
    float y = std::round((top + orMiddle - 8 - blockHeight) / 2);
    const float titleWidth = textWidth(title, metric::kTextNormal);
    const float titleX = std::round((width - metric::kIcon - 6 - titleWidth) / 2);
    icon(Icon::Qr, titleX, std::round(y + titleHeight / 2 - metric::kIcon / 2), metric::kIcon, tint);
    drawText(title, titleX + metric::kIcon + 6, textTop(y + titleHeight / 2, metric::kTextNormal), metric::kTextNormal,
             color::kText);
    y += titleHeight + 4;
    drawWrapped(message, kMargin, y, width - 2 * kMargin, metric::kTextSmall, color::kTextDim, 3, Align::Center);

    const std::string orText = tr("or");
    const float orWidth = textWidth(orText, metric::kTextSmall);
    rect(kMargin, std::round(orMiddle), (width - orWidth) / 2 - 8 - kMargin, 1, color::kDivider);
    rect((width + orWidth) / 2 + 8, std::round(orMiddle), (width - orWidth) / 2 - 8 - kMargin, 1, color::kDivider);
    drawText(orText, width / 2, textTop(orMiddle, metric::kTextSmall), metric::kTextSmall, color::kTextDim,
             Align::Center);

    roundedRect(fieldX_, fieldY_, fieldWidth_, fieldHeight_, metric::kRadius,
                fieldTouched_ ? mix(color::kSurfaceRaised, color::kText, 0.12f) : color::kSurfaceRaised);
    const float middle = fieldY_ + fieldHeight_ / 2;
    icon(Icon::Keyboard, fieldX_ + 12, std::round(middle - metric::kIcon / 2), metric::kIcon, color::kPrimaryLight);
    const float buttonX = fieldX_ + fieldWidth_ - 12 - ui::buttonWidth(KEY_X, metric::kButtonGlyph);
    drawText(tr("Enter a Quick ID"), fieldX_ + 12 + metric::kIcon + 10, textTop(middle, metric::kTextNormal),
             metric::kTextNormal, color::kText, Align::Left, buttonX - fieldX_ - 12 - metric::kIcon - 18);
    ui::drawHint(KEY_X, {}, buttonX, middle, color::kText, color::kTextDim);
}

} // namespace tz::scenes
