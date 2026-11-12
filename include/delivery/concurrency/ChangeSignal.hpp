#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace delivery::concurrency {
class ChangeSignal {
public:
    ChangeSignal() = default;

    ChangeSignal(const ChangeSignal&) = delete;
    ChangeSignal& operator=(const ChangeSignal&) = delete;

    std::uint64_t version() const;

    void notify();

    bool waitForChange(std::uint64_t seenVersion,
                       std::chrono::steady_clock::time_point deadline,
                       ShutdownToken& stop);

    void waitForChange(std::uint64_t seenVersion, ShutdownToken& stop);

    void wakeAll();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t version_ = 0;
};
}
