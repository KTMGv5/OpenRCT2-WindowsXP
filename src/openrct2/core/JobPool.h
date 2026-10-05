/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class JobPool
{
private:
    struct TaskData
    {
        std::function<void()> WorkFn;
        std::function<void()> CompletionFn;

        TaskData(std::function<void()> workFn, std::function<void()> completionFn);
    };

    bool _shouldStop = false;
    size_t _processing = 0;
    std::vector<std::thread> _threads;
    std::deque<TaskData> _pending;
    std::deque<TaskData> _completed;
    std::condition_variable _condPending;
    std::condition_variable _condComplete;
    std::mutex _mutex;

public:
    JobPool(size_t maxThreads = 255);
    ~JobPool();

    void AddTask(std::function<void()> workFn, std::function<void()> completionFn = nullptr);
    void Join(std::function<void()> reportFn = nullptr);
    bool IsBusy();

    /**
     * Executes @p fn in parallel for each index in [0, count) across worker threads
     * and the calling thread with minimal lock contention and zero dynamic memory allocations.
     */
    void ParallelFor(size_t count, const std::function<void(size_t index)>& fn);

    size_t GetThreadCount() const
    {
        return _threads.size();
    }

private:
    void ProcessQueue();
};
