/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "JobPool.h"

#include <cassert>
#include <chrono>

JobPool::TaskData::TaskData(std::function<void()> workFn, std::function<void()> completionFn)
    : WorkFn(std::move(workFn))
    , CompletionFn(std::move(completionFn))
{
}

JobPool::JobPool(size_t maxThreads)
{
    maxThreads = std::min<size_t>(maxThreads, std::max(1u, std::thread::hardware_concurrency()));
    for (size_t n = 0; n < maxThreads; n++)
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
    _condPending.notify_all();

    for (auto& th : _threads)
    {
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
        _pending.emplace_back(workFn, completionFn);
    }
    _condPending.notify_one();
}

void JobPool::Join(std::function<void()> reportFn)
{
    std::unique_lock lock(_mutex);
    while (true)
    {
        // Wait for the queue to become empty or having completed tasks.
        // Use wait_for with a timeout to avoid lost wakeup deadlocks on Windows XP winpthreads.
        _condComplete.wait_for(lock, std::chrono::milliseconds(50), [this]() {
            return (_pending.empty() && _processing == 0) || !_completed.empty() || _shouldStop;
        });

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
        if ((_completed.empty() && _pending.empty() && _processing == 0) || _shouldStop)
        {
            break;
        }
    }
}

bool JobPool::IsBusy()
{
    std::lock_guard lock(_mutex);
    return _processing != 0 || !_pending.empty();
}

void JobPool::ProcessQueue()
{
    std::unique_lock lock(_mutex);
    do
    {
        // Wait for work or cancellation with a timeout to avoid lost wakeup deadlocks on Windows XP winpthreads.
        _condPending.wait_for(lock, std::chrono::milliseconds(50), [this]() {
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

            taskData.WorkFn();

            lock.lock();

            _completed.push_back(std::move(taskData));

            _processing--;
            _condComplete.notify_all();
        }
    } while (!_shouldStop);
}
