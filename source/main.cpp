// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "app/app.hpp"
#include "sys/log.hpp"
#include "sys/session.hpp"

#include <3ds.h>
#include <atomic>
#include <cstdlib>
#include <cxxabi.h>
#include <exception>
#include <new>
#include <string>

// The linear heap (for the GPU and the DSP) gets 24 MB, and the heap the rest. libctru's default is 32 MB, of which the
// app uses about 12 MB, and 22 MB with a full badge set's preview: in the HOME Menu's 64 MB, that left the heap only
// about 29 MB, where downloads ran out of memory.
extern "C"
{
u32 __ctru_linear_heap_size = 24 * 1024 * 1024;
}

namespace
{

// The size of the last allocation operator new couldn't have, for onTerminate(). A nothrow new that fails sets it too.
std::atomic<size_t> failedAllocation{0};

// What stopped the app, for its report: the exception (main.cpp is built with exceptions for this), the allocation that
// failed, the thread, and the largest block the heap still has.
std::string describeFatal()
{
    // its strings need memory too: without any, the report still gets saved (15 characters or fewer need none)
    try
    {
        std::string why = "no exception";
        if (const std::type_info* type = abi::__cxa_current_exception_type())
        {
            why = type->name();
            try
            {
                throw;
            }
            catch (const std::exception& e)
            {
                why = why + ": " + e.what();
            }
            catch (...)
            {
            }
        }
        if (const size_t failed = failedAllocation)
            why += ", the last allocation that failed asked for " + std::to_string(failed) + " bytes";
        why += threadGetCurrent() ? ", on a worker thread" : ", on the main thread";
        size_t largest = 0;
        for (size_t step = size_t{1} << 26; step >= 4096; step /= 2)
        {
            if (void* volatile probe = std::malloc(largest + step))
            {
                std::free(probe);
                largest += step;
            }
        }
        return why + ", largest free block " + std::to_string(largest / 1024) + " KB";
    }
    catch (...)
    {
        return "out of memory";
    }
}

// Something the app can't go on from, like memory that ran out or a check in the C++ library: the log is saved for a
// bug report before the app stops.
[[noreturn]] void onTerminate()
{
    LOG_ERROR("the app stopped on a fatal error (%s; %s)", describeFatal().c_str(),
              tz::log::describe(tz::log::memory()).c_str());
    tz::session::saveNow(); // the next start offers to share it
    tz::log::saveNow(tz::kLogPath);
    std::abort();
}

} // namespace

// libstdc++'s, but noting the size that failed; its nothrow and array forms call this one.
void* operator new(std::size_t size)
{
    for (;;)
    {
        if (void* p = std::malloc(size ? size : 1))
            return p;
        const std::new_handler handler = std::get_new_handler();
        if (!handler)
            break;
        handler();
    }
    failedAllocation = size;
    throw std::bad_alloc();
}

int main(int argc, char** argv)
{
    std::set_terminate(onTerminate);
    return tz::App::get().run(argc, argv);
}
