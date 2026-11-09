#pragma once

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
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};
}
