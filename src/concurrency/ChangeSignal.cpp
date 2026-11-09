#include <delivery/concurrency/ChangeSignal.hpp>

namespace delivery::concurrency {
std::uint64_t ChangeSignal::version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

void ChangeSignal::notify() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++version_;
    }
    cv_.notify_all();
}

bool ChangeSignal::waitForChange(std::uint64_t seenVersion,
                                 std::chrono::steady_clock::time_point deadline,
                                 ShutdownToken& stop) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_until(lock, deadline, [&] {
        return version_ != seenVersion || stop.stopRequested();
    });
}

void ChangeSignal::wakeAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
}
}
