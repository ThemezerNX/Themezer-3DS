// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/item_ids.hpp"
#include "test.hpp"

using namespace tz;

TEST(item_ids_themezer)
{
    CHECK_EQ(ids::themezerQuickId("T1ED69").value_or(""), std::string("T1ED69"));
    CHECK_EQ(ids::themezerQuickId("  t1ed69 ").value_or(""), std::string("T1ED69"));
    CHECK_EQ(ids::themezerQuickId("S1EBAA").value_or(""), std::string("S1EBAA"));
    CHECK_EQ(ids::themezerQuickId("bs1b98f").value_or(""), std::string("BS1B98F"));
    CHECK(!ids::themezerQuickId("").has_value());
    CHECK(!ids::themezerQuickId("152151").has_value()); // ThemePlaza's
    CHECK(!ids::themezerQuickId("THEME").has_value());
    CHECK(!ids::themezerQuickId("T1ED6G").has_value());
    CHECK(!ids::themezerQuickId("T 1ED69").has_value());
    CHECK(!ids::themezerQuickId("T1ED69T1ED69T1ED69").has_value());
}

TEST(item_ids_themeplaza)
{
    CHECK_EQ(ids::themePlazaId("152151").value_or(""), std::string("152151"));
    CHECK_EQ(ids::themePlazaId(" #00152151 ").value_or(""), std::string("152151"));
    CHECK(!ids::themePlazaId("0").has_value());
    CHECK(!ids::themePlazaId("T1ED69").has_value());
    CHECK(!ids::themePlazaId("15 2151").has_value());
    CHECK(!ids::themePlazaId("1234567890").has_value());

    CHECK_EQ(ids::themePlazaIdInLink("https://themeplaza.art/download/152151").value_or(""), std::string("152151"));
    CHECK_EQ(ids::themePlazaIdInLink("http://www.themeplaza.art/item/12?x=1").value_or(""), std::string("12"));
    CHECK_EQ(ids::themePlazaIdInLink("http://themeplaza.art/download/152151/preview").value_or(""),
             std::string("152151"));
    CHECK(!ids::themePlazaIdInLink("https://themezer.net/3ds/themes/T1ED69").has_value());
    CHECK(!ids::themePlazaIdInLink("https://themeplaza.art.evil.example/download/1").has_value());
    CHECK(!ids::themePlazaIdInLink("https://themeplaza.art/user/152151").has_value());
    CHECK(!ids::themePlazaIdInLink("152151").has_value());
}

TEST(item_ids_in_file_names)
{
    // Themezer's tag and ThemePlaza's, wherever the rest of the name puts them, in any case
    CHECK(ids::fileNameHasId("Mario Theme by mira (Themezer-T1ED69)", "T1ED69"));
    CHECK(ids::fileNameHasId("mario (themezer-t1ed69) (2)", "T1ED69"));
    CHECK(ids::fileNameHasId("(Themezer-T1ED69) Mario by someone new", "T1ED69"));
    CHECK(ids::fileNameHasId("Mario by mira (152151)", "152151"));
    CHECK(ids::fileNameHasId("Mario by mira (152151) (2)", "152151"));
    // a longer or another ID, a bare one, or none
    CHECK(!ids::fileNameHasId("Mario (Themezer-T1ED690)", "T1ED69"));
    CHECK(!ids::fileNameHasId("Mario (Themezer-T1ED6)", "T1ED69"));
    CHECK(!ids::fileNameHasId("Mario T1ED69", "T1ED69"));
    CHECK(!ids::fileNameHasId("Mario (1521510)", "152151"));
    CHECK(!ids::fileNameHasId("Mario (Themezer-T1ED69)", ""));
}
