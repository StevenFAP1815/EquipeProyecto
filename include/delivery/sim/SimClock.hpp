#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace delivery::sim {
class SimClock {
public:
    explicit SimClock(double timeScale);

    SimClock(const SimClock&) = delete;
    SimClock& operator=(const SimClock&) = delete;

    std::int64_t nowMs() const noexcept;

    std::chrono::steady_clock::time_point realTimeAt(std::int64_t simMs) const;

    bool waitUntilMs(std::int64_t targetMs, concurrency::ShutdownToken& stop);

    void wakeAll();

    double timeScale() const noexcept { return timeScale_; }

private:
    const double timeScale_;
    const std::chrono::steady_clock::time_point startReal_;

    std::mutex mutex_;
    std::condition_variable cv_;
};
}
