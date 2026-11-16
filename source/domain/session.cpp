// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/session.hpp"

#include <charconv>
#include <cstdio>

namespace tz::session
{

namespace
{

constexpr std::string_view kMagic = "Themezer-3DS session";

template <typename T> bool number(std::string_view text, T& out, int base = 10)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return error == std::errc() && end == text.data() + text.size();
}

std::string hex8(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08lX", static_cast<unsigned long>(value));
    return text;
}

std::string describe(const LumaDump& dump)
{
    constexpr const char* kTypes[] = {"FIQ", "undefined instruction", "prefetch abort", "data abort"};
    std::string text = dump.type < 4 ? kTypes[dump.type] : "exception " + std::to_string(dump.type);
    text += " at PC " + hex8(dump.pc) + " (LR " + hex8(dump.lr) + ", SP " + hex8(dump.sp) + ")";
    if (dump.type == 3)
        text += ", accessing " + hex8(dump.far);
    return text + ", in " + dump.process;
}

} // namespace

std::string toText(const Snapshot& snapshot)
{
    char header[256];
    std::snprintf(header, sizeof(header),
                  "%s\nversion %s\ntitle %016llX\nlumaDumps %lu\nstalledMs %lu\nmemory %lu %lu %lu %lu\nlog\n",
                  kMagic.data(), snapshot.version.c_str(), static_cast<unsigned long long>(snapshot.titleId),
                  static_cast<unsigned long>(snapshot.lumaDumps), static_cast<unsigned long>(snapshot.stalledMs),
                  static_cast<unsigned long>(snapshot.heap), static_cast<unsigned long>(snapshot.heapUsed),
                  static_cast<unsigned long>(snapshot.linear), static_cast<unsigned long>(snapshot.linearFree));
    std::string text = header;
    for (const std::string& line : snapshot.log)
        text += line + '\n';
    return text;
}

std::optional<Snapshot> parse(std::string_view text)
{
    Snapshot snapshot;
    bool inLog = false;
    bool first = true;
    while (!text.empty())
    {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (first)
        {
            if (line != kMagic)
                return std::nullopt;
            first = false;
            continue;
        }
        if (inLog)
        {
            snapshot.log.emplace_back(line);
            continue;
        }
        if (line == "log")
        {
            inLog = true;
            continue;
        }
        const size_t space = line.find(' ');
        const std::string_view key = line.substr(0, space);
        const std::string_view value = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
        if (key == "version")
            snapshot.version = value;
        else if (key == "title")
            (void)number(value, snapshot.titleId, 16);
        else if (key == "lumaDumps")
            (void)number(value, snapshot.lumaDumps);
        else if (key == "stalledMs")
            (void)number(value, snapshot.stalledMs);
        else if (key == "memory")
        {
            uint32_t* fields[] = {&snapshot.heap, &snapshot.heapUsed, &snapshot.linear, &snapshot.linearFree};
            std::string_view rest = value;
            for (uint32_t* field : fields)
            {
                const size_t gap = rest.find(' ');
                (void)number(rest.substr(0, gap), *field);
                rest = gap == std::string_view::npos ? std::string_view() : rest.substr(gap + 1);
            }
        }
    }
    if (!inLog)
        return std::nullopt;
    return snapshot;
}

std::optional<LumaDump> parseLumaDump(ByteSpan data)
{
    constexpr size_t kHeaderSize = 0x28;
    constexpr uint16_t kArm11 = 11;
    constexpr size_t kRegisters = 20; // r0 to r12, sp, lr, pc, cpsr, dfsr, ifsr, far (and FPU ones after them)
    if (data.size() < kHeaderSize || readU32(data, 0) != 0xDEADC0DE || readU32(data, 4) != 0xDEADCAFE ||
        (data[8 + 4] | data[8 + 5] << 8) != kArm11)
        return std::nullopt;
    const uint32_t registerSize = readU32(data, 0x18);
    const uint64_t additional = uint64_t{kHeaderSize} + registerSize + readU32(data, 0x1C) + readU32(data, 0x20);
    if (registerSize < kRegisters * 4 || kHeaderSize + registerSize > data.size() || readU32(data, 0x24) < 16 ||
        additional + 16 > data.size())
        return std::nullopt;

    LumaDump dump;
    dump.type = readU32(data, 0x10);
    const auto reg = [&](size_t i) { return readU32(data, kHeaderSize + i * 4); };
    for (size_t i = 0; i < dump.r.size(); ++i)
        dump.r[i] = reg(i);
    dump.sp = reg(13);
    dump.lr = reg(14);
    dump.pc = reg(15);
    dump.cpsr = reg(16);
    dump.far = reg(19);
    const size_t at = static_cast<size_t>(additional);
    for (size_t i = 0; i < 8 && data[at + i] != 0; ++i)
        dump.process += static_cast<char>(data[at + i]);
    dump.titleId = uint64_t(readU32(data, at + 8)) | uint64_t(readU32(data, at + 12)) << 32;
    return dump;
}

std::optional<uint32_t> lumaDumpNumber(std::string_view fileName)
{
    constexpr std::string_view kPrefix = "crash_dump_";
    constexpr std::string_view kSuffix = ".dmp";
    if (!fileName.starts_with(kPrefix) || !fileName.ends_with(kSuffix))
        return std::nullopt;
    uint32_t value = 0;
    if (!number(fileName.substr(kPrefix.size(), fileName.size() - kPrefix.size() - kSuffix.size()), value))
        return std::nullopt;
    return value;
}

Ending ending(const Snapshot& snapshot, const std::optional<LumaDump>& dump)
{
    const bool crashed = dump && dump->titleId == snapshot.titleId;
    // main.cpp's onTerminate() line: what stopped it, and the memory then
    for (auto line = snapshot.log.rbegin(); line != snapshot.log.rend(); ++line)
    {
        const size_t at = line->find("the app stopped on a fatal error");
        if (at == std::string::npos)
            continue;
        std::string detail = line->substr(at);
        if (crashed)
            detail += "; " + describe(*dump);
        return Ending{"fatal error", std::move(detail)};
    }
    if (crashed)
        return Ending{"crash", describe(*dump)};
    if (snapshot.stalledMs >= kFreezeMs)
        return Ending{"freeze", "the main loop had stopped for " + std::to_string(snapshot.stalledMs / 1000) + " s"};
    return Ending{"unclean exit", "no crash dump of it: Luma's not saved (a button other than A), or the power went"};
}

} // namespace tz::session
