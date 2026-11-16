// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/log.hpp"

#include "app_version.h"
#include "sys/thread.hpp"

#include <3ds.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <malloc.h>
#include <mutex>
#include <string>

namespace tz::log
{

namespace
{
constexpr size_t kKeptLines = 128;
Mutex mutex;
std::array<std::string, kKeptLines> lines;
size_t next = 0;
size_t count = 0;
std::atomic<bool> errors{false};
std::atomic<uint32_t> written{0};
std::atomic<void (*)()> listener{nullptr};

std::vector<std::string> keptLines()
{
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(lines[(next + kKeptLines - count + i) % kKeptLines]);
    return result;
}

void writeLines(const char* path)
{
    FILE* file = std::fopen(path, "w");
    if (!file)
        return;
    std::fprintf(file, "%s %s\n", APP_NAME, APP_VERSION);
    for (size_t i = 0; i < count; ++i)
        std::fprintf(file, "%s\n", lines[(next + kKeptLines - count + i) % kKeptLines].c_str());
    std::fclose(file);
}
} // namespace

void init()
{
    consoleDebugInit(debugDevice_SVC);
}

void write(const char* level, const char* format, ...)
{
    char text[512];
    const u64 ms = osGetTime();
    int prefix = std::snprintf(text, sizeof(text), "%02llu:%02llu:%02llu.%03llu %s ", (ms / 3600000) % 24,
                               (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000, level);
    if (prefix < 0)
        prefix = 0;
    va_list args;
    va_start(args, format);
    std::vsnprintf(text + prefix, sizeof(text) - prefix, format, args);
    va_end(args);

    std::fprintf(stderr, "%s\n", text);
    if (level[0] == 'E')
        errors = true;

    {
        std::lock_guard lock(mutex);
        lines[next] = text;
        next = (next + 1) % kKeptLines;
        if (count < kKeptLines)
            ++count;
        ++written;
    }
    if (void (*const notify)() = listener)
        notify();
}

void writeError(const char* what, const Error& error)
{
    write("E", "%s: %s (%08lX) %s", what, errcName(error.code), static_cast<unsigned long>(error.system),
          error.detail.c_str());
}

Memory memory()
{
    const struct mallinfo heap = mallinfo();
    return {static_cast<uint32_t>(envGetHeapSize() / 1024), static_cast<uint32_t>(heap.uordblks / 1024),
            static_cast<uint32_t>(envGetLinearHeapSize() / 1024), static_cast<uint32_t>(linearSpaceFree() / 1024)};
}

std::string describe(const Memory& memory)
{
    char text[96];
    std::snprintf(text, sizeof(text), "heap %lu of %lu KB in use, linear %lu of %lu KB free",
                  static_cast<unsigned long>(memory.heapUsed), static_cast<unsigned long>(memory.heap),
                  static_cast<unsigned long>(memory.linearFree), static_cast<unsigned long>(memory.linear));
    return text;
}

void save(const char* path)
{
    std::lock_guard lock(mutex);
    writeLines(path);
}

std::vector<std::string> tail(size_t wanted)
{
    std::lock_guard lock(mutex);
    const size_t n = std::min(wanted, count);
    std::vector<std::string> result;
    for (size_t i = count - n; i < count; ++i)
        result.push_back(lines[(next + kKeptLines - count + i) % kKeptLines]);
    return result;
}

std::vector<std::string> tailNow()
{
    const bool locked = mutex.try_lock();
    std::vector<std::string> result = keptLines();
    if (locked)
        mutex.unlock();
    return result;
}

uint32_t changes()
{
    return written;
}

void setListener(void (*notify)())
{
    listener = notify;
}

bool hadErrors()
{
    return errors;
}

void saveNow(const char* path)
{
    // without the lock when another thread holds it: a line may come out torn, but the rest is there
    const bool locked = mutex.try_lock();
    writeLines(path);
    if (locked)
        mutex.unlock();
}

} // namespace tz::log
