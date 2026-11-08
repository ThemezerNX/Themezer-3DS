// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/report.hpp"
#include "test.hpp"

using namespace tz;

TEST(report_shareable)
{
    for (const Errc code : {Errc::NotFound, Errc::InvalidData, Errc::Unsupported, Errc::Io, Errc::OutOfMemory})
        CHECK(report::shareable(code));
    // not the app's: the network, the SD card's room, the user, files too big for the HOME Menu, what is in use
    for (const Errc code : {Errc::TooLarge, Errc::NoSpace, Errc::Cancelled, Errc::NoNetwork, Errc::Network, Errc::Tls,
                            Errc::Http, Errc::Busy, Errc::NotAllowed})
        CHECK(!report::shareable(code));
}

TEST(report_json)
{
    report::Report report;
    report.version = "1.2.0";
    report.device = "New 3DS XL";
    report.region = "EUR";
    report.firmware = "11.17.0";
    report.error = Error{Errc::InvalidData, static_cast<int32_t>(0xE0E083F2), "a \"quoted\"\nline\\ \x01"};
    report.log = {"first", "tab\there"};
    CHECK_EQ(report::toJson(report),
             std::string("{\"version\":\"1.2.0\",\"device\":\"New 3DS XL\",\"region\":\"EUR\",\"firmware\":\"11.17.0\","
                         "\"error\":\"invalid data\",\"system\":\"E0E083F2\","
                         "\"detail\":\"a \\\"quoted\\\"\\nline\\\\ \\u0001\",\"log\":[\"first\",\"tab\\there\"]}"));
}

TEST(report_json_limits)
{
    report::Report report;
    // invalid UTF-8 becomes U+FFFD; texts are cut at 512 characters; every line of the log goes, in order
    report.error.detail = std::string("bad \xFF byte") + std::string(1000, 'x');
    for (int i = 0; i < 128; ++i)
        report.log.push_back("line " + std::to_string(i));
    const std::string json = report::toJson(report);
    CHECK(json.find("bad \xEF\xBF\xBD byte") != std::string::npos);
    CHECK(json.find(std::string(502, 'x')) != std::string::npos);
    CHECK(json.find(std::string(503, 'x')) == std::string::npos);
    CHECK(json.find("[\"line 0\",\"line 1\",") != std::string::npos);
    CHECK(json.find(",\"line 127\"]}") != std::string::npos);
}
