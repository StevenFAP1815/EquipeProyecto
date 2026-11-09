#include <delivery/concurrency/ShutdownToken.hpp>

namespace delivery::concurrency {
void ShutdownToken::requestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
}

bool ShutdownToken::stopRequested() const noexcept {
    return stop_;
}

void ShutdownToken::waitForStop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return stop_.load(); });
}

bool ShutdownToken::waitForStopUntil(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_until(lock, deadline, [this] { return stop_.load(); });
}
}
