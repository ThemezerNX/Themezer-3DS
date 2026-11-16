// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/app.hpp"
#include "sys/log.hpp"

#include <3ds.h>
#include <cstdlib>
#include <exception>

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
