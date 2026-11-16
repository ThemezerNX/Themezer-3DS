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

TEST(item_ids_site_of_file_name)
{
    // the names the sites give their downloads
    CHECK(ids::siteOfFileName("Korn Theme by isaac-rizzi (Themezer-T1ED6C).zip") == ids::Site::Themezer);
    CHECK(ids::siteOfFileName("korn (THEMEZER-t1ed6c).zip") == ids::Site::Themezer);
    CHECK(ids::siteOfFileName("Sasuke eyes by tvpiv (144651).zip") == ids::Site::ThemePlaza);
    // anything else, as far as the app can tell
    CHECK(ids::siteOfFileName("download.zip") == ids::Site::Elsewhere);
    CHECK(ids::siteOfFileName("Mario (2).zip") == ids::Site::Elsewhere); // a copy's number, not an ID
    CHECK(ids::siteOfFileName("Mario (#144651).zip") == ids::Site::Elsewhere);
    CHECK(ids::siteOfFileName("Mario (v2).zip") == ids::Site::Elsewhere);
    CHECK(ids::siteOfFileName("Mario (.zip") == ids::Site::Elsewhere);
}

TEST(item_ids_id_in_file_name)
{
    // where it is, so that the name and the author can be read from before it
    const auto themezer = ids::idInFileName("Korn Theme by isaac-rizzi (themezer-t1ed6c) (2)");
    REQUIRE(themezer.has_value());
    CHECK(themezer->site == ids::Site::Themezer);
    CHECK_EQ(themezer->id, std::string("T1ED6C"));
    CHECK_EQ(themezer->start, size_t(26));
    CHECK_EQ(themezer->end, size_t(43));
    // the last number of ThemePlaza's size, after one in the name and before a copy's
    const auto plaza = ids::idInFileName("Pokemon (2023) by someone (152151) (2)");
    REQUIRE(plaza.has_value());
    CHECK(plaza->site == ids::Site::ThemePlaza);
    CHECK_EQ(plaza->id, std::string("152151"));
    // Themezer's tag over a number in the name
    const auto song = ids::idInFileName("Song (2023) Theme by x (Themezer-T1ED69)");
    REQUIRE(song.has_value());
    CHECK_EQ(song->id, std::string("T1ED69"));
    // Themezer's plain hexadecimal ID, without its kind's letters
    const auto plain = ids::idInFileName("Korn Theme by isaac-rizzi (Themezer-1ed6c)");
    REQUIRE(plain.has_value());
    CHECK(plain->site == ids::Site::Themezer);
    CHECK_EQ(plain->id, std::string("1ED6C"));
    CHECK(ids::fileNameHasId("Korn Theme by isaac-rizzi (Themezer-1ED6C)", "1ed6c"));
    CHECK(ids::siteOfFileName("Korn (Themezer-123456).zip") == ids::Site::Themezer);
    // not an ID: a bad quick ID, padding, a leading zero, too few or too many digits
    for (const char* name : {"Mario (Themezer-)", "Mario (Themezer-T1ED6G)", "Mario (Themezer-1ED6G)",
                             "Mario (Themezer- T1)", "Mario (Themezer-THEME)", "Mario (0152151)",
                             "Mario (12)", "Mario (1234567890)", "Mario ( 152151)", "Mario"})
        CHECK(!ids::idInFileName(name).has_value());
}
