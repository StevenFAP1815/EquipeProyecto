#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace delivery::concurrency {
class GpuWorkerStopped : public std::runtime_error {
public:
    GpuWorkerStopped() : std::runtime_error("GPU worker is stopped") {}
};

class GpuWorker {
public:
    GpuWorker();
    ~GpuWorker();

    GpuWorker(const GpuWorker&) = delete;
    GpuWorker& operator=(const GpuWorker&) = delete;

    template <class Job>
    std::future<std::invoke_result_t<Job>> submit(Job job) {
        using Result = std::invoke_result_t<Job>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(job));
        std::future<Result> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw GpuWorkerStopped();
            }
            jobs_.emplace_back([task] { (*task)(); });
        }
        jobAvailable_.notify_one();
        return result;
    }

    void stop();

    std::size_t pendingJobs() const;
    std::size_t completedJobs() const noexcept;
    std::thread::id workerThreadId() const noexcept;

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable jobAvailable_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::atomic<std::size_t> completed_{0};
    std::thread thread_;
};
}
