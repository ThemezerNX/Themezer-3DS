// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Translations. The English text is the key: tr("Install") gives the text in the console's language when a
// translation file has it (romfs:/lang/<code>.json, an object of English to translated text), and the English
// otherwise. "{}" in a text is filled in by format(), in order. The keys are the string literals in tr(), trf() and
// trn(), which tools/i18n.py finds: give them literals, not variables.

#pragma once

#include <string>
#include <string_view>

namespace tz
{

void loadTranslations(int systemLanguage);
const char* tr(const char* english);

// Replaces each "{}" in pattern with the next argument.
std::string format(std::string_view pattern, std::initializer_list<std::string_view> args);

template <class... Args>
std::string trf(const char* english, const Args&... args)
{
    return format(tr(english), {std::string_view(args)...});
}

// trn()'s text before format() fills it in.
const char* trPlural(const char* one, const char* other, unsigned long count);

// The form of a text for a count, which fills its first "{}". English has one and other (count 1, and the rest); a
// translation file has the other's text as the key, and an array of the language's forms as the value, in the order of
// core/plural.hpp's pluralForm(): "{} badges": ["{} значок", "{} значка", "{} значков"].
template <class... Args>
std::string trn(const char* one, const char* other, unsigned long count, const Args&... args)
{
    const std::string number = std::to_string(count);
    return format(trPlural(one, other, count), {std::string_view(number), std::string_view(args)...});
}

} // namespace tz
