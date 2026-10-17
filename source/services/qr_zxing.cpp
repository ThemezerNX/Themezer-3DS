// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Built with exceptions, as ZXing-cpp is: nothing it throws gets past this file, short of running out of memory in code
// it shares with the rest of the app, which closes the app there as anywhere else.

#include "services/qr_zxing.hpp"

#include "ReadBarcode.h"

namespace tz
{

std::optional<std::string> readQrCode(const uint8_t* grey, int width, int height)
{
    try
    {
        static const ZXing::ReaderOptions options = ZXing::ReaderOptions()
                                                        .formats(ZXing::BarcodeFormat::QRCode)
                                                        // the bytes as they are: by default NULs and other control
                                                        // characters come back as "<NUL>" and so on
                                                        .textMode(ZXing::TextMode::Plain)
                                                        .tryRotate(false)
                                                        .tryInvert(false)
                                                        .tryDownscale(false)
                                                        .maxNumberOfSymbols(1);
        const ZXing::Barcode barcode =
            ZXing::ReadBarcode(ZXing::ImageView(grey, width, height, ZXing::ImageFormat::Lum), options);
        if (barcode.isValid())
            return barcode.text();
    }
    catch (...)
    {
    }
    return std::nullopt;
}

} // namespace tz
