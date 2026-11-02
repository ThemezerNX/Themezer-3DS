// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/strings.hpp"

#include "core/plural.hpp"

#include <3ds.h>
#include <algorithm>
#include <jansson.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace tz
{

namespace
{
// a text's forms: one, or a plural's (trn())
std::unordered_map<std::string, std::vector<std::string>> translations;
const char* language = "en";

const char* languageCode(int language)
{
    switch (language)
    {
    case CFG_LANGUAGE_JP:
        return "ja";
    case CFG_LANGUAGE_FR:
        return "fr";
    case CFG_LANGUAGE_DE:
        return "de";
    case CFG_LANGUAGE_IT:
        return "it";
    case CFG_LANGUAGE_ES:
        return "es";
    case CFG_LANGUAGE_ZH:
        return "zh-Hans";
    case CFG_LANGUAGE_KO:
        return "ko";
    case CFG_LANGUAGE_NL:
        return "nl";
    case CFG_LANGUAGE_PT:
        return "pt";
    case CFG_LANGUAGE_RU:
        return "ru";
    case CFG_LANGUAGE_TW:
        return "zh-Hant";
    default:
        return "en";
    }
}
} // namespace

void loadTranslations(int systemLanguage)
{
    translations.clear();
    language = languageCode(systemLanguage);
    const std::string path = std::string("romfs:/lang/") + language + ".json";
    json_error_t error;
    json_t* root = json_load_file(path.c_str(), 0, &error);
    if (!root)
        return;
    const char* key;
    json_t* value;
    json_object_foreach(root, key, value)
    {
        // an empty text isn't translated yet
        std::vector<std::string> forms;
        if (json_is_string(value) && json_string_length(value) > 0)
            forms.emplace_back(json_string_value(value));
        size_t i;
        json_t* form;
        json_array_foreach(value, i, form)
        {
            if (!json_is_string(form) || json_string_length(form) == 0)
            {
                forms.clear();
                break;
            }
            forms.emplace_back(json_string_value(form));
        }
        if (!forms.empty())
            translations.emplace(key, std::move(forms));
    }
    json_decref(root);
}

const char* tr(const char* english)
{
    if (translations.empty())
        return english;
    auto it = translations.find(english);
    return it == translations.end() ? english : it->second.front().c_str();
}

const char* trPlural(const char* one, const char* other, unsigned long count)
{
    auto it = translations.find(other);
    if (it == translations.end())
        return count == 1 ? one : other;
    const std::vector<std::string>& forms = it->second;
    return forms[std::min(pluralForm(language, count), forms.size() - 1)].c_str();
}

std::string format(std::string_view pattern, std::initializer_list<std::string_view> args)
{
    std::string out;
    out.reserve(pattern.size() + 32);
    auto arg = args.begin();
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}' && arg != args.end())
        {
            out.append(*arg++);
            ++i;
        }
        else
        {
            out += pattern[i];
        }
    }
    return out;
}

} // namespace tz
