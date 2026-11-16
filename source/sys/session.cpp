// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/session.hpp"

#include "app_version.h"
#include "domain/session.hpp"
#include "sys/log.hpp"
#include "sys/thread.hpp"

#include <3ds.h>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace tz::session
{

namespace
{

constexpr const char* kLumaDumps = "/luma/dumps/arm11";
constexpr uint64_t kMaxDumpSize = 64 * 1024;
constexpr uint64_t kMaxSnapshotSize = 256 * 1024;
// a change is written at once, the next not before kWriteGapNs; a stall, each second it goes on
constexpr s64 kWriteEveryNs = 1000 * 1000000LL;
constexpr s64 kWriteGapNs = 100 * 1000000LL;
// the main loop stopped this long is worth writing down, a second at a time
constexpr uint64_t kStallMs = 2000;

std::string sessionPath;     // within the SD card
std::string sessionFilePath; // for stdio, "sdmc:..."
Snapshot base;               // what doesn't change during the session

std::atomic<uint64_t> lastFrameMs{0};
std::atomic<int> held{0};
std::atomic<bool> suspended{false};
aptHookCookie hookCookie;

Thread writer;
Mutex mutex;
ConditionVariable wake;
bool stopping = false;

// The highest number among Luma's dumps, 0 without any.
uint32_t lastLumaDump(FileSystem& sd, std::string* name = nullptr)
{
    uint32_t highest = 0;
    if (auto entries = sd.list(kLumaDumps))
        for (const DirEntry& entry : *entries)
            if (const auto number = lumaDumpNumber(entry.name); number && *number > highest)
            {
                highest = *number;
                if (name)
                    *name = entry.name;
            }
    return highest;
}

// How long the main loop has stopped, while the app is in front and nothing holds it up on purpose.
uint32_t stalledMs()
{
    const uint64_t last = lastFrameMs;
    if (last == 0 || suspended || held > 0)
        return 0;
    const uint64_t now = osGetTime();
    return now > last ? static_cast<uint32_t>(now - last) : 0;
}

void write(std::vector<std::string> lines, uint32_t stalled)
{
    Snapshot snapshot = base;
    snapshot.stalledMs = stalled;
    const log::Memory memory = log::memory();
    snapshot.heap = memory.heap;
    snapshot.heapUsed = memory.heapUsed;
    snapshot.linear = memory.linear;
    snapshot.linearFree = memory.linearFree;
    snapshot.log = std::move(lines);
    const std::string text = toText(snapshot);
    if (FILE* file = std::fopen(sessionFilePath.c_str(), "w"))
    {
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }
}

void run()
{
    uint32_t writtenLog = ~0u;
    uint32_t writtenStall = 0;
    std::unique_lock lock(mutex);
    while (!stopping)
    {
        const uint32_t changes = log::changes();
        uint32_t stalled = stalledMs();
        if (stalled < kStallMs)
            stalled = 0;
        if (changes != writtenLog || stalled / 1000 != writtenStall / 1000)
        {
            lock.unlock();
            write(log::tail(SIZE_MAX), stalled);
            svcSleepThread(kWriteGapNs);
            lock.lock();
            writtenLog = changes;
            writtenStall = stalled;
        }
        wake.waitFor(mutex, kWriteEveryNs);
    }
}

// A new line in the log: written now, so that a crash right after it leaves it.
void onLogLine()
{
    wake.notifyAll();
}

void onApt(APT_HookType type, void*)
{
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP)
        suspended = true;
    else if (type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP)
    {
        lastFrameMs = osGetTime();
        suspended = false;
    }
}

} // namespace

std::optional<report::Report> takeLastSession(FileSystem& sd, std::string_view folder)
{
    const std::string path = std::string(folder) + "/session.txt";
    Expected<Bytes> data = sd.readFile(path, kMaxSnapshotSize);
    if (!data)
        return std::nullopt;
    const std::string last = std::string(folder) + "/last-session.txt";
    (void)sd.removeFile(last);
    (void)sd.rename(path, last);

    const auto snapshot = parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    if (!snapshot)
        return std::nullopt;
    // the newest of Luma's dumps, when it came after the session started (its title tells whose it is)
    std::optional<LumaDump> dump;
    std::string name;
    if (lastLumaDump(sd, &name) > snapshot->lumaDumps)
        if (Expected<Bytes> bytes = sd.readFile(std::string(kLumaDumps) + "/" + name, kMaxDumpSize))
            dump = parseLumaDump(*bytes);
    const Ending how = ending(*snapshot, dump);
    LOG_WARN("the last session ended badly (%s): %s", how.kind.c_str(), how.detail.c_str());

    report::Report report;
    report.version = snapshot->version;
    report.heap = snapshot->heap;
    report.heapUsed = snapshot->heapUsed;
    report.linear = snapshot->linear;
    report.linearFree = snapshot->linearFree;
    report.errorName = how.kind;
    report.error.detail = how.detail;
    report.log = snapshot->log;
    return report;
}

void begin(FileSystem& sd, std::string_view folder)
{
    sessionPath = std::string(folder) + "/session.txt";
    sessionFilePath = "sdmc:" + sessionPath;
    base.version = APP_VERSION;
    // as Luma3DS writes it in its dumps; through its own call when APT doesn't say (a 3dsx)
    u64 title = 0;
    s64 lumaTitle = 0;
    if (R_SUCCEEDED(APT_GetProgramID(&title)) && title != 0)
        base.titleId = title;
    else if (R_SUCCEEDED(svcGetProcessInfo(&lumaTitle, CUR_PROCESS_HANDLE, 0x10001)))
        base.titleId = static_cast<uint64_t>(lumaTitle);
    base.lumaDumps = lastLumaDump(sd);
    aptHook(&hookCookie, onApt, nullptr);
    write(log::tail(SIZE_MAX), 0);
    // on the system core, so that it goes on when the main thread spins
    writer = Thread::start(run, priority::kWorker, 1, 16 * 1024);
    log::setListener(onLogLine);
}

void frame()
{
    lastFrameMs = osGetTime();
}

void holdWatch(bool hold)
{
    held += hold ? 1 : -1;
    if (!hold)
        lastFrameMs = osGetTime();
}

void saveNow()
{
    if (!sessionFilePath.empty())
        write(log::tailNow(), stalledMs());
}

void end()
{
    if (sessionPath.empty())
        return;
    log::setListener(nullptr);
    {
        std::lock_guard lock(mutex);
        stopping = true;
        wake.notifyAll();
    }
    writer.join();
    aptUnhook(&hookCookie);
    (void)std::remove(sessionFilePath.c_str());
    sessionPath.clear();
}

} // namespace tz::session
