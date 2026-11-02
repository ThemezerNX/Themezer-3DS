// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "core/plural.hpp"

namespace tz
{

size_t pluralForm(std::string_view language, unsigned long count)
{
    if (language == "ja" || language == "ko" || language.starts_with("zh"))
        return 0;
    if (language == "fr")
        return count <= 1 ? 0 : 1;
    if (language == "ru")
    {
        const unsigned long ones = count % 10, tens = count % 100;
        if (ones == 1 && tens != 11)
            return 0;
        if (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14))
            return 1;
        return 2;
    }
    return count == 1 ? 0 : 1;
}

} // namespace tz
