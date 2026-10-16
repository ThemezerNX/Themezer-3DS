// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/jobs.hpp"

#include "sys/thread.hpp"

#include <3ds.h>
#include <deque>
#include <mutex>
#include <vector>

namespace tz::jobs
{

namespace
{

struct Job
{
    int priority;
    uint64_t sequence;
    CancelToken token;
    std::function<void(const CancelToken&)> work;
};

struct Worker
{
    Mutex mutex;
    ConditionVariable wake;
    std::vector<Job> queue; // a heap would save little for the few jobs queued at once
    CancelToken running;    // the token of the job that runs now, for shutdown()
    bool stopping = false;
    Thread thread;
};

Worker workers[static_cast<size_t>(Lane::Count)];
std::atomic<uint64_t> sequence{0};

Mutex postedMutex;
std::deque<std::function<void()>> posted;

void workerLoop(Worker& worker)
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock(worker.mutex);
            while (worker.queue.empty() && !worker.stopping)
                worker.wake.wait(worker.mutex);
            if (worker.stopping)
                return;

            // the highest priority, and the oldest among equals
            auto best = worker.queue.begin();
            for (auto it = worker.queue.begin(); it != worker.queue.end(); ++it)
            {
                if (it->priority > best->priority ||
                    (it->priority == best->priority && it->sequence < best->sequence))
                    best = it;
            }
            job = std::move(*best);
            worker.queue.erase(best);
            worker.running = job.token;
        }

        if (!job.token.cancelled())
            job.work(job.token);
    }
}

} // namespace

void init()
{
    for (size_t lane = 0; lane < std::size(workers); ++lane)
    {
        Worker& worker = workers[lane];
        worker.stopping = false;
        // the 3DS doesn't share a core between threads of one priority: the icons go before a preview's decoding,
        // and the background errands after everything
        const int priority = lane == static_cast<size_t>(Lane::Icons)        ? priority::kIcons
                             : lane == static_cast<size_t>(Lane::Background) ? priority::kIdle
                                                                             : priority::kWorker;
        worker.thread = Thread::start([&worker] { workerLoop(worker); }, priority, -2, 128 * 1024);
    }
}

void shutdown()
{
    for (Worker& worker : workers)
    {
        {
            std::lock_guard lock(worker.mutex);
            for (Job& job : worker.queue)
                job.token.cancel();
            worker.queue.clear();
            // a download running now stops instead of keeping the app from closing until it's done
            worker.running.cancel();
            worker.stopping = true;
        }
        worker.wake.notifyAll();
    }
    for (Worker& worker : workers)
        worker.thread.join();

    std::lock_guard lock(postedMutex);
    posted.clear();
}

void run(Lane lane, CancelToken token, std::function<void(const CancelToken&)> work, int priority)
{
    Worker& worker = workers[static_cast<size_t>(lane)];
    {
        std::lock_guard lock(worker.mutex);
        if (worker.stopping)
            return;
        worker.queue.push_back(Job{priority, sequence++, std::move(token), std::move(work)});
    }
    worker.wake.notifyOne();
}

void post(std::function<void()> function)
{
    std::lock_guard lock(postedMutex);
    posted.push_back(std::move(function));
}

void pump(uint32_t budgetMs)
{
    const u64 start = osGetTime();
    for (;;)
    {
        std::function<void()> function;
        {
            std::lock_guard lock(postedMutex);
            if (posted.empty())
                return;
            function = std::move(posted.front());
            posted.pop_front();
        }
        function();
        if (osGetTime() - start >= budgetMs)
            return;
    }
}

} // namespace tz::jobs
