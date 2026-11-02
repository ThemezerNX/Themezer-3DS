// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include <cstddef>
#include <string_view>

namespace tz
{

// Which of a language's plural forms a count takes, as an index into the forms a translation gives: CLDR's cardinal
// rules for the console's languages (app/strings's codes), for whole numbers. Japanese, Chinese and Korean have one
// form; Russian has three (1, 21 / 2, 22 / 5, 11); the others two, singular and plural, where French counts 0 as
// singular.
size_t pluralForm(std::string_view language, unsigned long count);

} // namespace tz
