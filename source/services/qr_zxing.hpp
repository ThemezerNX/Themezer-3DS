// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tz
{

// The text of a QR code in an 8-bit grey picture, read with ZXing-cpp; nullopt when there's none.
std::optional<std::string> readQrCode(const uint8_t* grey, int width, int height);

} // namespace tz
