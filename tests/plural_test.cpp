// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "core/plural.hpp"
#include "test.hpp"

using namespace tz;

TEST(plural_two_forms)
{
    for (const char* language : {"en", "de", "es", "it", "nl", "pt"})
    {
        CHECK_EQ(pluralForm(language, 1), 0u);
        CHECK_EQ(pluralForm(language, 0), 1u);
        CHECK_EQ(pluralForm(language, 2), 1u);
        CHECK_EQ(pluralForm(language, 21), 1u);
    }
    // French counts 0 as singular
    CHECK_EQ(pluralForm("fr", 0), 0u);
    CHECK_EQ(pluralForm("fr", 1), 0u);
    CHECK_EQ(pluralForm("fr", 2), 1u);
}

TEST(plural_one_form)
{
    for (const char* language : {"ja", "ko", "zh-Hans", "zh-Hant"})
        for (unsigned long count : {0ul, 1ul, 2ul, 5ul})
            CHECK_EQ(pluralForm(language, count), 0u);
}

TEST(plural_russian)
{
    // 1 значок, 2 значка, 5 значков
    for (unsigned long count : {1ul, 21ul, 101ul})
        CHECK_EQ(pluralForm("ru", count), 0u);
    for (unsigned long count : {2ul, 4ul, 22ul, 24ul})
        CHECK_EQ(pluralForm("ru", count), 1u);
    for (unsigned long count : {0ul, 5ul, 11ul, 12ul, 14ul, 25ul, 111ul})
        CHECK_EQ(pluralForm("ru", count), 2u);
}
