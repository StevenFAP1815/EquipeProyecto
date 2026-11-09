#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace delivery::concurrency {
class ShutdownToken {
public:
    ShutdownToken() = default;

    ShutdownToken(const ShutdownToken&) = delete;
    ShutdownToken& operator=(const ShutdownToken&) = delete;

    void requestStop();

    bool stopRequested() const noexcept;

    void waitForStop();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};
}
