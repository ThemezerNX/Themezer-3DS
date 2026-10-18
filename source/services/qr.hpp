// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Scanning QR codes with the outer camera.

#pragma once

#include "core/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tz
{

class QrScanner
{
public:
    // The camera's whole picture (VGA). The top screen's size would be a crop of it scaled down, giving a code's
    // modules 1.6 times fewer pixels: it would read only from close by and straight on.
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    // Starts the outer camera, and looks for QR codes in its pictures, on threads of their own: the camera driver
    // takes up to a second to start, and as long to stop, so neither holds up the caller. Fails only when out of
    // memory; the camera's own errors come from failure().
    static Expected<std::unique_ptr<QrScanner>> start();
    ~QrScanner(); // stops the camera, waiting for it unless stopped() already

    // Why the camera didn't start, once it's known: Busy when the camera is in use, NotAllowed when the camera
    // service can't be used (some ways of starting homebrew don't allow it).
    std::optional<Error> failure() const;

    // Asks the camera to stop, without waiting for it; stopped() once it has, and then the scanner goes at once.
    void stop();
    bool stopped() const;

    QrScanner(const QrScanner&) = delete;
    QrScanner& operator=(const QrScanner&) = delete;

    // Copies the newest picture, kWidth x kHeight RGB565 in rows from the top, if there is one since the last call.
    bool latestFrame(std::vector<uint16_t>& frame);

    // The text of the first QR code found.
    std::optional<std::string> result() const;

    struct Impl;

private:
    explicit QrScanner(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace tz
