// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "scenes/errors.hpp"
#include "services/qr.hpp"
#include "ui/input.hpp"

#include <citro3d.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tz::scenes
{

// The QR code tab: the outer camera on the top screen, read until it sees a QR code; on the bottom screen what to do,
// and a field to type an item's ID instead, for when the camera can't read the code.
class QrView
{
public:
    QrView();
    ~QrView();
    QrView(const QrView&) = delete;
    QrView& operator=(const QrView&) = delete;

    // Turns the camera on, shortly after (or says why it can't be); stop() turns it off.
    void start();
    void stop();
    bool scanning() const { return scanner_ != nullptr || starting_; }

    // Every frame, shown or not: lets a stopped camera go once it's off.
    void tick();

    // Every frame while shown: shows the newest picture, and returns the text of a QR code once one is read (the
    // camera stops then, until start()).
    std::optional<std::string> update();

    // Every frame while shown: whether the ID field was tapped (it shows pressed while touched).
    bool idFieldTapped(const ui::Input& input);

    // The camera's picture fills the top screen.
    bool showsPicture() const { return scanner_ && hasFrame_; }
    void drawTop();
    void drawBottom(float top, float bottom);

private:
    void open();
    void showProblem(const Error& error);

    std::unique_ptr<QrScanner> scanner_;
    // stopped, until its threads have turned the camera off; another opens only after it
    std::unique_ptr<QrScanner> closing_;
    // start() was asked: the camera opens in an update() from openAt_ (osGetTime), once the tab is on screen
    bool starting_ = false;
    u64 openAt_ = 0;
    std::optional<ErrorInfo> problem_; // why the camera can't be used
    bool scanned_ = false;            // a code was read: the camera waits to be started again
    C3D_Tex texture_{};
    bool textureReady_ = false;
    bool hasFrame_ = false;
    std::vector<uint16_t> frame_;
    // the ID field, where drawBottom last drew it
    float fieldX_ = 0, fieldY_ = 0, fieldWidth_ = 0, fieldHeight_ = 0;
    bool fieldTouched_ = false;
};

} // namespace tz::scenes
