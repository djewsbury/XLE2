// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Mutex.h"
#include "LockFree.h"
#include <vector>
#include <thread>
#include <functional>

namespace Utility
{
    class CompletionThreadPool
    {
    public:
        template<class Fn, class... Args>
            void Enqueue(Fn&& fn, Args&&... args);

		void EnqueueBasic(std::function<void()>&& task);

        bool IsGood() const { return !_workerThreads.empty(); }

        CompletionThreadPool(unsigned threadCount);
        ~CompletionThreadPool();

        CompletionThreadPool(const CompletionThreadPool&) = delete;
        CompletionThreadPool& operator=(const CompletionThreadPool&) = delete;
        CompletionThreadPool(CompletionThreadPool&&) = delete;
        CompletionThreadPool& operator=(CompletionThreadPool&&) = delete;
    private:
        std::vector<std::thread> _workerThreads;
        
        Threading::Mutex _pendingsTaskLock;
        typedef std::function<void()> PendingTask;
        LockFree::FixedSizeQueue<PendingTask, 256> _pendingTasks;

        XlHandle _events[2];
        volatile bool _workerQuit;
    };

    template<class Fn, class... Args>
        void CompletionThreadPool::Enqueue(Fn&& fn, Args&&... args)
        {
			EnqueueBasic(std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        }

    class ThreadPool
    {
    public:
        template<class Fn, class... Args>
            void Enqueue(Fn&& fn, Args&&... args);

		void EnqueueBasic(std::function<void()>&& task);

        bool IsGood() const { return !_workerThreads.empty(); }

        ThreadPool(unsigned threadCount);
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;
    private:
        std::vector<std::thread> _workerThreads;

        Threading::Conditional _pendingTaskVariable;
        Threading::Mutex _pendingTaskLock;
        typedef std::function<void()> PendingTask;
        LockFree::FixedSizeQueue<PendingTask, 256> _pendingTasks;

        volatile bool _workerQuit;
    };

    template<class Fn, class... Args>
        void ThreadPool::Enqueue(Fn&& fn, Args&&... args)
        {
			EnqueueBasic(std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        }
}

using namespace Utility;
