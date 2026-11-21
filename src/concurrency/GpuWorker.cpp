#include <delivery/concurrency/GpuWorker.hpp>

namespace delivery::concurrency {
GpuWorker::GpuWorker() : thread_([this] { run(); }) {}

GpuWorker::~GpuWorker() {
    stop();
}

void GpuWorker::stop() {
    std::deque<std::function<void()>> discarded;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        discarded.swap(jobs_);
    }
    jobAvailable_.notify_all();
    discarded.clear();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::size_t GpuWorker::pendingJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

std::size_t GpuWorker::completedJobs() const noexcept {
    return completed_.load();
}

std::thread::id GpuWorker::workerThreadId() const noexcept {
    return thread_.get_id();
}

void GpuWorker::run() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            jobAvailable_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
        completed_.fetch_add(1);
    }
}
}
