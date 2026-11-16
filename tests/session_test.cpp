// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/session.hpp"
#include "test.hpp"

using namespace tz;

namespace
{

// A real one: a 3dsx's data abort, reading address 0xC (a null pointer's field), on a console with Luma3DS.
const uint8_t kLumaDump[] = {
    0xDE, 0xC0, 0xAD, 0xDE, 0xFE, 0xCA, 0xAD, 0xDE, 0x03, 0x00, 0x01, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x0C, 0x01, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xE8, 0xC6, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x7C, 0x2D, 0x12, 0x00, 0x38, 0xC1, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xE0, 0x10, 0x12, 0x00, 0xE0, 0x10, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x80, 0x00, 0x08, 0xE8, 0x7F, 0x00, 0x08,
    0x98, 0xA1, 0x11, 0x00, 0x5C, 0x9C, 0x11, 0x00, 0x10, 0x00, 0x07, 0x60, 0x05, 0x00, 0x00, 0x00,
    0x0A, 0x10, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xE3, 0x7A, 0xB4, 0xEE,
    0xE3, 0x7A, 0xB4, 0xEE, 0x00, 0x30, 0x80, 0x25, 0x35, 0x00, 0x00, 0x2A, 0x00, 0x70, 0xA0, 0xE1,
    0xD4, 0x00, 0x9F, 0xE5, 0x04, 0x10, 0x8D, 0xE5, 0x53, 0x01, 0x00, 0xEB, 0xCC, 0x30, 0x9F, 0xE5,
    0x04, 0x10, 0x9D, 0xE5, 0x01, 0x41, 0x93, 0xE7, 0x00, 0x00, 0x54, 0xE3, 0x28, 0x00, 0x00, 0x0A,
    0x00, 0x50, 0xA0, 0xE3, 0x04, 0x60, 0x94, 0xE5, 0xAC, 0x00, 0x9F, 0xE5, 0x01, 0x60, 0x46, 0xE2,
    0x04, 0x60, 0x84, 0xE5, 0x01, 0x51, 0x83, 0xE7, 0x4E, 0x01, 0x00, 0xEB, 0x05, 0x00, 0x56, 0xE1,
    0x19, 0x00, 0x00, 0x1A, 0x00, 0x20, 0x94, 0xE5, 0x94, 0x30, 0x9F, 0xE5, 0x02, 0x11, 0x93, 0xE7,
    0x0C, 0x50, 0x91, 0xE5, 0x05, 0xCF, 0x12, 0x00, 0x02, 0x00, 0x00, 0x00, 0x60, 0xC2, 0x12, 0x00,
    0x60, 0xC2, 0x12, 0x00, 0xE0, 0x10, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x64, 0x73, 0x78,
    0x5F, 0x61, 0x70, 0x70, 0x00, 0x1E, 0x92, 0x0D, 0x00, 0x00, 0x04, 0x00
};

session::Snapshot sample()
{
    session::Snapshot snapshot;
    snapshot.version = "1.2.2";
    snapshot.titleId = 0x000400000D921E00;
    snapshot.lumaDumps = 38;
    snapshot.stalledMs = 0;
    snapshot.heap = 37000;
    snapshot.heapUsed = 6000;
    snapshot.linear = 24576;
    snapshot.linearFree = 13000;
    snapshot.log = {"01:02:03.004 I themezer-3ds 1.2.2 started", "01:02:04.005 W music: once"};
    return snapshot;
}

} // namespace

TEST(session_snapshot_text)
{
    const session::Snapshot snapshot = sample();
    const std::string text = session::toText(snapshot);
    CHECK(text.starts_with("Themezer-3DS session\nversion 1.2.2\ntitle 000400000D921E00\n"));
    const auto back = session::parse(text);
    REQUIRE(back.has_value());
    CHECK_EQ(back->version, snapshot.version);
    CHECK_EQ(back->titleId, snapshot.titleId);
    CHECK_EQ(back->lumaDumps, snapshot.lumaDumps);
    CHECK_EQ(back->heapUsed, snapshot.heapUsed);
    CHECK_EQ(back->linearFree, snapshot.linearFree);
    CHECK(back->log == snapshot.log);
    // cut short before its log, or not one at all
    CHECK(!session::parse(text.substr(0, text.find("log\n"))).has_value());
    CHECK(!session::parse("themezer-3ds 1.2.2\n01:02:03.004 I started\n").has_value());
    CHECK(!session::parse("").has_value());
}

TEST(session_luma_dump)
{
    const auto dump = session::parseLumaDump(ByteSpan(kLumaDump, sizeof(kLumaDump)));
    REQUIRE(dump.has_value());
    CHECK_EQ(dump->type, uint32_t(3));
    CHECK_EQ(dump->pc, uint32_t(0x00119C5C));
    CHECK_EQ(dump->lr, uint32_t(0x0011A198));
    CHECK_EQ(dump->sp, uint32_t(0x08007FE8));
    CHECK_EQ(dump->far, uint32_t(0x0000000C));
    CHECK_EQ(dump->process, std::string("3dsx_app"));
    CHECK_EQ(dump->titleId, uint64_t(0x000400000D921E00));
    // cut short anywhere, or not one
    for (size_t size = 0; size < sizeof(kLumaDump); ++size)
        CHECK(!session::parseLumaDump(ByteSpan(kLumaDump, size)).has_value());
    Bytes arm9(kLumaDump, kLumaDump + sizeof(kLumaDump));
    arm9[0xC] = 9;
    CHECK(!session::parseLumaDump(arm9).has_value());

    CHECK_EQ(session::lumaDumpNumber("crash_dump_00000039.dmp").value_or(0), uint32_t(39));
    CHECK_EQ(session::lumaDumpNumber("crash_dump_00000040.dmp").value_or(0), uint32_t(40));
    CHECK(!session::lumaDumpNumber("crash_dump_0000003A.dmp").has_value());
    CHECK(!session::lumaDumpNumber("crash_dump_.dmp").has_value());
    CHECK(!session::lumaDumpNumber("notes.txt").has_value());
}

TEST(session_ending)
{
    const auto dump = session::parseLumaDump(ByteSpan(kLumaDump, sizeof(kLumaDump)));
    REQUIRE(dump.has_value());
    session::Snapshot snapshot = sample();

    // this app's dump: a crash, and where
    const auto crash = session::ending(snapshot, dump);
    CHECK_EQ(crash.kind, std::string("crash"));
    CHECK_EQ(crash.detail, std::string("data abort at PC 00119C5C (LR 0011A198, SP 08007FE8), accessing 0000000C, "
                                        "in 3dsx_app"));
    // another title's dump is not this app's crash: without one, the app just didn't close (a dump not saved, the power)
    session::Snapshot other = snapshot;
    other.titleId = 0x0004000000123400;
    CHECK_EQ(session::ending(other, dump).kind, std::string("unclean exit"));
    CHECK_EQ(session::ending(snapshot, std::nullopt).kind, std::string("unclean exit"));

    // a main loop that had stopped long enough: a freeze
    snapshot.stalledMs = session::kFreezeMs - 1;
    CHECK_EQ(session::ending(snapshot, std::nullopt).kind, std::string("unclean exit"));
    snapshot.stalledMs = 12500;
    const auto freeze = session::ending(snapshot, std::nullopt);
    CHECK_EQ(freeze.kind, std::string("freeze"));
    CHECK_EQ(freeze.detail, std::string("the main loop had stopped for 12 s"));

    // the app's own fatal error, with the dump it left
    snapshot.log.push_back("01:02:05.006 E the app stopped on a fatal error (heap 1 of 2 KB in use)");
    const auto fatal = session::ending(snapshot, dump);
    CHECK_EQ(fatal.kind, std::string("fatal error"));
    CHECK(fatal.detail.starts_with("the app stopped on a fatal error (heap 1 of 2 KB in use); data abort at PC"));
}
