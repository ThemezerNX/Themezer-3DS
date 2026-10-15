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

QrView::QrView()
{
    textureReady_ = C3D_TexInit(&texture_, 512, 256, GPU_RGB565);
    if (textureReady_)
        C3D_TexSetFilter(&texture_, GPU_LINEAR, GPU_LINEAR);
}

QrView::~QrView()
{
    scanner_.reset();
    if (textureReady_)
        C3D_TexDelete(&texture_);
}

void QrView::start()
{
    if (scanner_)
        return;
    scanned_ = false;
    hasFrame_ = false;
    problem_.reset();
    auto scanner = QrScanner::start();
    if (scanner)
    {
        scanner_ = std::move(*scanner);
        return;
    }
    if (scanner.error().code == Errc::NotAllowed && App::get().homebrew())
        problem_ = ErrorInfo{tr("The camera isn't available"),
                             tr("Started this way, the app can't use the camera. Install the CIA version to scan QR "
                                "codes.")};
    else if (scanner.error().code == Errc::Busy)
        problem_ = ErrorInfo{tr("The camera is busy"), tr("Something else is using the camera. Try again in a moment.")};
    else
        problem_ = describe(scanner.error());
}

void QrView::stop()
{
    scanner_.reset();
    hasFrame_ = false;
}

std::optional<std::string> QrView::update()
{
    if (!scanner_)
        return std::nullopt;
    if (textureReady_ && scanner_->latestFrame(frame_))
    {
        // rows of RGB565 into the GPU's tiles, a frame at a time
        auto* texels = static_cast<uint16_t*>(texture_.data);
        for (int y = 0; y < QrScanner::kHeight; ++y)
            for (int x = 0; x < QrScanner::kWidth; ++x)
                texels[tiledIndex(x, y, 512)] = frame_[static_cast<size_t>(y) * QrScanner::kWidth + x];
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
        if (scanner_)
            spinner(metric::kTopWidth / 2, metric::kHeight / 2, 14, color::kText);
        return;
    }
    drawTexturePart(&texture_, 0, 0, QrScanner::kWidth, QrScanner::kHeight, 0, 0, metric::kTopWidth, metric::kHeight,
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
