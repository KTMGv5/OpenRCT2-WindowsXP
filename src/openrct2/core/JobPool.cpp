/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "JobPool.h"

#include <algorithm>
#include <cassert>
#include <chrono>

JobPool::TaskData::TaskData(std::function<void()> workFn, std::function<void()> completionFn)
    : WorkFn(std::move(workFn))
    , CompletionFn(std::move(completionFn))
{
}

JobPool::JobPool(size_t maxThreads)
{
    size_t numCores = std::max(1u, std::thread::hardware_concurrency());
    size_t targetThreads = std::max<size_t>(1u, numCores);
    if (maxThreads == 255)
    {
        // For general job pools, if multi-core, reserve 1 core for the calling thread
        // so background threads don't oversubscribe the CPU.
        targetThreads = (numCores > 1) ? (numCores - 1) : 1;
    }
    else
    {
        targetThreads = std::min(maxThreads, numCores);
    }
    targetThreads = std::max<size_t>(1u, targetThreads);
    _threads.reserve(targetThreads);
    for (size_t n = 0; n < targetThreads; n++)
    {
        _threads.emplace_back(&JobPool::ProcessQueue, this);
    }
}

JobPool::~JobPool()
{
    {
        std::lock_guard lock(_mutex);
        _shouldStop = true;
        _pending.clear();
        _condPending.notify_all();
    }

    for (auto& th : _threads)
    {
        _condPending.notify_all();
        if (th.joinable())
        {
            th.join();
        }
    }
}

void JobPool::AddTask(std::function<void()> workFn, std::function<void()> completionFn)
{
    {
        std::lock_guard lock(_mutex);
        _pending.emplace_back(std::move(workFn), std::move(completionFn));
    }
    _condPending.notify_one();
}

void JobPool::Join(std::function<void()> reportFn)
{
    std::unique_lock lock(_mutex);

    // Calling thread helps process pending tasks to minimize idle latency
    while (!_pending.empty())
    {
        auto taskData = std::move(_pending.front());
        _pending.pop_front();
        _processing++;

        lock.unlock();

        try
        {
            taskData.WorkFn();
        }
        catch (...)
        {
        }

        lock.lock();

        _processing--;
        if (taskData.CompletionFn)
        {
            _completed.push_back(std::move(taskData));
        }
    }

    while (true)
    {
        // Dispatch all completion callbacks if there are any.
        while (!_completed.empty())
        {
            auto taskData = std::move(_completed.front());
            _completed.pop_front();

            if (taskData.CompletionFn)
            {
                lock.unlock();

                taskData.CompletionFn();

                lock.lock();
            }
        }

        if (reportFn)
        {
            lock.unlock();

            reportFn();

            lock.lock();
        }

        // If everything is empty and no more work has to be done we can stop waiting.
        if (_completed.empty() && _pending.empty() && _processing == 0)
        {
            break;
        }

        if (_shouldStop)
        {
            break;
        }

        // Wait for workers to finish. 2ms timeout to avoid lost wakeup deadlocks on Windows XP winpthreads.
        _condComplete.wait_for(lock, std::chrono::milliseconds(2), [this]() {
            return (_pending.empty() && _processing == 0) || !_completed.empty() || _shouldStop;
        });
    }
}

bool JobPool::IsBusy()
{
    std::lock_guard lock(_mutex);
    return _processing != 0 || !_pending.empty();
}

void JobPool::ParallelFor(size_t count, const std::function<void(size_t index)>& fn)
{
    if (count == 0)
    {
        return;
    }

    if (count == 1 || _threads.empty())
    {
        for (size_t i = 0; i < count; i++)
        {
            try
            {
                fn(i);
            }
            catch (...)
            {
            }
        }
        return;
    }

    std::atomic<size_t> currentIndex(0);
    const size_t numWorkers = std::min(_threads.size(), count - 1);

    {
        std::lock_guard lock(_mutex);
        for (size_t i = 0; i < numWorkers; i++)
        {
            _pending.emplace_back([&currentIndex, count, &fn]() {
                while (true)
                {
                    size_t idx = currentIndex.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= count)
                    {
                        break;
                    }
                    try
                    {
                        fn(idx);
                    }
                    catch (...)
                    {
                    }
                }
            }, nullptr);
        }
    }
    _condPending.notify_all();

    // Calling thread also processes items directly
    while (true)
    {
        size_t idx = currentIndex.fetch_add(1, std::memory_order_relaxed);
        if (idx >= count)
        {
            break;
        }
        try
        {
            fn(idx);
        }
        catch (...)
        {
        }
    }

    Join();
}

void JobPool::ProcessQueue()
{
    std::unique_lock lock(_mutex);
    while (true)
    {
        // Wait for work or cancellation with a 2ms timeout to avoid lost wakeup deadlocks on Windows XP winpthreads.
        _condPending.wait_for(lock, std::chrono::milliseconds(2), [this]() {
            return _shouldStop || !_pending.empty();
        });

        if (_shouldStop && _pending.empty())
        {
            break;
        }

        if (!_pending.empty())
        {
            _processing++;

            auto taskData = std::move(_pending.front());
            _pending.pop_front();

            lock.unlock();

            try
            {
                taskData.WorkFn();
            }
            catch (...)
            {
            }

            lock.lock();

            _processing--;

            if (taskData.CompletionFn)
            {
                _completed.push_back(std::move(taskData));
                _condComplete.notify_one();
            }
            else if (_pending.empty() && _processing == 0)
            {
                _condComplete.notify_all();
            }
        }
    }
}
