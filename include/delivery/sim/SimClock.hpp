#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace delivery::sim {
class SimClock {
public:
    explicit SimClock(double timeScale);
    ~SimClock();

    SimClock(const SimClock&) = delete;
    SimClock& operator=(const SimClock&) = delete;

    std::int64_t nowMs() const noexcept;

    bool waitUntilMs(std::int64_t targetMs, concurrency::ShutdownToken& stop);

    void start(concurrency::ShutdownToken& stop);

    void stop();

    double timeScale() const noexcept { return timeScale_; }

private:
    void tickerLoop(concurrency::ShutdownToken& stop);

    const double timeScale_;
    const std::chrono::steady_clock::time_point startReal_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread ticker_;
};
}
