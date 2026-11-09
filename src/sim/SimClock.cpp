#include <delivery/sim/SimClock.hpp>

#include <chrono>

namespace delivery::sim {
namespace {
constexpr int kTickerIntervalMs = 10;
}

SimClock::SimClock(double timeScale)
    : timeScale_(timeScale),
      startReal_(std::chrono::steady_clock::now()) {}

SimClock::~SimClock() {
}

std::int64_t SimClock::nowMs() const noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - startReal_;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return static_cast<std::int64_t>(static_cast<double>(elapsedMs) * timeScale_);
}

bool SimClock::waitUntilMs(std::int64_t targetMs,
                           concurrency::ShutdownToken& stop) {
    if (nowMs() >= targetMs) {
        return !stop.stopRequested();
    }

    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [&] {
        return nowMs() >= targetMs || stop.stopRequested();
    });

    return !stop.stopRequested();
}

void SimClock::start(concurrency::ShutdownToken& stop) {
    if (ticker_.joinable()) {
        return;
    }
    ticker_ = std::thread([this, &stop] {
        tickerLoop(stop);
    });
}

void SimClock::stop() {
    if (ticker_.joinable()) {
        ticker_.join();
    }
}

void SimClock::tickerLoop(concurrency::ShutdownToken& stop) {
    while (!stop.stopRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickerIntervalMs));
        cv_.notify_all();
    }
    cv_.notify_all();
}
}
