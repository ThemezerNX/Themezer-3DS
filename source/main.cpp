// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/app.hpp"
#include "sys/log.hpp"

#include <3ds.h>
#include <cstdlib>
#include <exception>

// The linear heap (for the GPU and the DSP) gets 24 MB, and the heap the rest. libctru's default is 32 MB, of which the
// app uses about 12 MB, and 22 MB with a full badge set's preview: in the HOME Menu's 64 MB, that left the heap only
// about 29 MB, where downloads ran out of memory.
extern "C"
{
u32 __ctru_linear_heap_size = 24 * 1024 * 1024;
}

namespace
{

// Something the app can't go on from, like memory that ran out or a check in the C++ library: the log is saved for a
// bug report before the app stops.
[[noreturn]] void onTerminate()
{
    LOG_ERROR("the app stopped on a fatal error (%s)", tz::log::describe(tz::log::memory()).c_str());
    tz::log::saveNow(tz::kLogPath);
    std::abort();
}

} // namespace

int main(int argc, char** argv)
{
    std::set_terminate(onTerminate);
    return tz::App::get().run(argc, argv);
}
