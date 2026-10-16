// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/app.hpp"
#include "sys/log.hpp"

#include <3ds.h>
#include <cstdlib>
#include <exception>
#include <malloc.h>

namespace
{

// Something the app can't go on from, like memory that ran out or a check in the C++ library: the log is saved for a
// bug report before the app stops.
[[noreturn]] void onTerminate()
{
    const struct mallinfo heap = mallinfo();
    LOG_ERROR("the app stopped on a fatal error (heap %u KB in use, %u KB free; linear %lu KB free)",
              static_cast<unsigned>(heap.uordblks / 1024), static_cast<unsigned>(heap.fordblks / 1024),
              static_cast<unsigned long>(linearSpaceFree() / 1024));
    tz::log::saveNow(tz::kLogPath);
    std::abort();
}

} // namespace

int main(int argc, char** argv)
{
    std::set_terminate(onTerminate);
    return tz::App::get().run(argc, argv);
}
