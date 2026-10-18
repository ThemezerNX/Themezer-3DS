// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/thread.hpp"

namespace tz
{

Thread& Thread::operator=(Thread&& other) noexcept
{
    if (this != &other)
    {
        join();
        handle_ = other.handle_;
        kernel_ = other.kernel_;
        state_ = std::move(other.state_);
        other.handle_ = nullptr;
    }
    return *this;
}

void Thread::entry(void* arg)
{
    auto* state = static_cast<State*>(arg);
    LightEvent_Wait(&state->go);
    state->function();
}

Thread Thread::start(std::function<void()> function, int priority, int core, size_t stackSize)
{
    Thread thread;
    thread.state_ = std::make_unique<State>();
    thread.state_->function = std::move(function);
    LightEvent_Init(&thread.state_->go, RESET_STICKY);
    thread.handle_ = threadCreate(entry, thread.state_.get(), stackSize, priority, core, false);
    if (!thread.handle_ && core != -2)
    {
        // the system core isn't available to every app: fall back to ours
        thread.handle_ = threadCreate(entry, thread.state_.get(), stackSize, priority, -2, false);
    }
    if (!thread.handle_)
    {
        thread.state_.reset();
        return thread;
    }
    // the thread waits at the start for this, so it can't have ended yet
    thread.kernel_ = threadGetHandle(thread.handle_);
    LightEvent_Signal(&thread.state_->go);
    return thread;
}

bool Thread::done() const
{
    return !handle_ || svcWaitSynchronization(kernel_, 0) == 0;
}

void Thread::join()
{
    if (!handle_)
        return;
    // The kernel's handle, not threadJoin(): that returns once libctru's flag says the thread finished, and the thread
    // still writes to the memory threadFree() frees after setting it.
    svcWaitSynchronization(kernel_, U64_MAX);
    threadFree(handle_);
    handle_ = nullptr;
    state_.reset();
}

void sleepMs(u32 milliseconds)
{
    svcSleepThread(static_cast<s64>(milliseconds) * 1000000LL);
}

} // namespace tz
