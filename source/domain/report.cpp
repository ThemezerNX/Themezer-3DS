// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "domain/report.hpp"

#include "core/utf.hpp"

#include <cstdio>

namespace tz::report
{

namespace
{

constexpr size_t kMaxText = 512;
constexpr size_t kMaxLogLines = 24;

// A JSON string: invalid UTF-8 becomes U+FFFD, and the text is cut at kMaxText code points.
void appendString(std::string& out, std::string_view text)
{
    out += '"';
    text = utf::prefix(text, kMaxText);
    size_t index = 0;
    while (index < text.size())
    {
        const char32_t c = utf::decodeNext(text, index);
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04X", static_cast<unsigned>(c));
                out += escape;
            }
            else
            {
                utf::appendUtf8(out, c);
            }
        }
    }
    out += '"';
}

void appendField(std::string& out, const char* name, std::string_view value)
{
    out += '"';
    out += name;
    out += "\":";
    appendString(out, value);
    out += ',';
}

} // namespace

bool shareable(Errc code)
{
    switch (code)
    {
    case Errc::NotFound:
    case Errc::InvalidData:
    case Errc::Unsupported:
    case Errc::Io:
    case Errc::OutOfMemory:
        return true;
    case Errc::TooLarge:
    case Errc::NoSpace:
    case Errc::Cancelled:
    case Errc::NoNetwork:
    case Errc::Network:
    case Errc::Tls:
    case Errc::Http:
    case Errc::Busy:
    case Errc::NotAllowed:
        break;
    }
    return false;
}

std::string toJson(const Report& report)
{
    std::string out = "{";
    appendField(out, "version", report.version);
    appendField(out, "device", report.device);
    appendField(out, "region", report.region);
    appendField(out, "firmware", report.firmware);
    appendField(out, "error", errcName(report.error.code));
    char system[16];
    std::snprintf(system, sizeof(system), "%08X", static_cast<unsigned>(report.error.system));
    appendField(out, "system", system);
    appendField(out, "detail", report.error.detail);
    out += "\"log\":[";
    const size_t first = report.log.size() > kMaxLogLines ? report.log.size() - kMaxLogLines : 0;
    for (size_t i = first; i < report.log.size(); ++i)
    {
        if (i > first)
            out += ',';
        appendString(out, report.log[i]);
    }
    out += "]}";
    return out;
}

} // namespace tz::report
