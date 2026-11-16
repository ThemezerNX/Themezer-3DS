// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/item_ids.hpp"

#include "core/path.hpp"

#include <algorithm>
#include <cctype>

namespace tz::ids
{

namespace
{

constexpr size_t kMaxQuickIdLength = 16;
constexpr size_t kMaxThemePlazaDigits = 9;

std::string_view trim(std::string_view text)
{
    constexpr std::string_view kSpace = " \t\r\n";
    const size_t start = text.find_first_not_of(kSpace);
    if (start == std::string_view::npos)
        return {};
    return text.substr(start, text.find_last_not_of(kSpace) - start + 1);
}

} // namespace

std::optional<std::string> themezerQuickId(std::string_view text)
{
    const std::string_view trimmed = trim(text);
    if (trimmed.empty() || trimmed.size() > kMaxQuickIdLength)
        return std::nullopt;
    std::string id;
    size_t letters = 0; // the kind's, before the number
    for (const char c : trimmed)
    {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const bool letter = upper >= 'A' && upper <= 'Z';
        const bool hex = (upper >= '0' && upper <= '9') || (upper >= 'A' && upper <= 'F');
        // in "BS1B98F", the B after the 1 is a digit; until a digit comes, letters are the kind's
        const bool digitSeen = std::any_of(id.begin(), id.end(), [](char d) { return d >= '0' && d <= '9'; });
        if (letter && !digitSeen)
            ++letters;
        else if (!hex)
            return std::nullopt;
        id += upper;
    }
    const bool hasDigit = std::any_of(id.begin(), id.end(), [](char d) { return d >= '0' && d <= '9'; });
    if (letters == 0 || !hasDigit)
        return std::nullopt;
    return id;
}

std::optional<std::string> themePlazaId(std::string_view text)
{
    std::string_view digits = trim(text);
    if (digits.starts_with('#'))
        digits.remove_prefix(1);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string_view::npos)
        return std::nullopt;
    digits.remove_prefix(std::min(digits.find_first_not_of('0'), digits.size()));
    if (digits.empty() || digits.size() > kMaxThemePlazaDigits)
        return std::nullopt;
    return std::string(digits);
}

std::optional<std::string> themePlazaIdInLink(std::string_view url)
{
    url = trim(url);
    for (std::string_view scheme : {"http://", "https://"})
    {
        if (!url.starts_with(scheme))
            continue;
        std::string_view rest = url.substr(scheme.size());
        if (rest.starts_with("www."))
            rest.remove_prefix(4);
        constexpr std::string_view kHost = "themeplaza.art/";
        if (!rest.starts_with(kHost))
            return std::nullopt;
        rest.remove_prefix(kHost.size());
        for (std::string_view section : {"download/", "item/"})
        {
            if (!rest.starts_with(section))
                continue;
            rest.remove_prefix(section.size());
            return themePlazaId(rest.substr(0, rest.find_first_of("/?#")));
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<IdInName> idInFileName(std::string_view fileName)
{
    constexpr std::string_view kThemezerTag = "themezer-";
    constexpr size_t kMinThemePlazaDigits = 3; // shortcut: not a copy's "(2)", nor ThemePlaza's first 99 items
    std::optional<IdInName> themezer;
    std::optional<IdInName> themePlaza;
    for (size_t open = fileName.find('('); open != std::string_view::npos; open = fileName.find('(', open + 1))
    {
        const size_t close = fileName.find(')', open);
        if (close == std::string_view::npos)
            break;
        const std::string_view inside = fileName.substr(open + 1, close - open - 1);
        if (inside.size() > kThemezerTag.size() &&
            path::equalsIgnoreCase(inside.substr(0, kThemezerTag.size()), kThemezerTag))
        {
            // a quick ID ("T1ED69"), or the plain hexadecimal one without its kind's letters ("1ED69")
            const std::string_view written = inside.substr(kThemezerTag.size());
            std::optional<std::string> id = themezerQuickId(written);
            if (!id && written.size() <= kMaxQuickIdLength &&
                written.find_first_not_of("0123456789ABCDEFabcdef") == std::string_view::npos)
            {
                id = std::string(written);
                std::transform(id->begin(), id->end(), id->begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
            if (id && path::equalsIgnoreCase(*id, written))
                themezer = IdInName{Site::Themezer, std::move(*id), open, close + 1};
        }
        else if (inside.size() >= kMinThemePlazaDigits && !inside.starts_with('0'))
        {
            if (auto id = themePlazaId(inside); id && *id == inside)
                themePlaza = IdInName{Site::ThemePlaza, std::move(*id), open, close + 1};
        }
    }
    return themezer ? themezer : themePlaza;
}

bool fileNameHasId(std::string_view fileName, std::string_view id)
{
    const auto found = idInFileName(fileName);
    return found && !id.empty() && path::equalsIgnoreCase(found->id, id);
}

Site siteOfFileName(std::string_view fileName)
{
    const auto found = idInFileName(fileName);
    return found ? found->site : Site::Elsewhere;
}

} // namespace tz::ids
