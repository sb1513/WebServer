#include "thread_pool.h"

ThreadPool::ThreadPool(size_t numThreads)
    : stop_(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    condition_.wait(lock, [this] {
                        return stop_.load() || !tasks_.empty();
                    });

                    if (stop_.load() && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stop_.load()) {
            return;
        }

        tasks_.push(std::move(task));
    }

    condition_.notify_one();
}

void ThreadPool::shutdown() {
    bool expected = false;

    if (!stop_.compare_exchange_strong(expected, true)) {
        return;
    }

    condition_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}