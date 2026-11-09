#include <delivery/sim/SimClock.hpp>

namespace delivery::sim {
SimClock::SimClock(double timeScale)
    : timeScale_(timeScale),
      startReal_(std::chrono::steady_clock::now()) {}

std::int64_t SimClock::nowMs() const noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - startReal_;
    const double elapsedRealMs =
        std::chrono::duration<double, std::milli>(elapsed).count();
    return static_cast<std::int64_t>(elapsedRealMs * timeScale_);
}

bool SimClock::waitUntilMs(std::int64_t targetMs,
                           concurrency::ShutdownToken& stop) {
    const std::chrono::duration<double, std::milli> realOffset(
        static_cast<double>(targetMs) / timeScale_);
    const auto deadline =
        startReal_ +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(realOffset);

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_until(lock, deadline, [&stop] { return stop.stopRequested(); });

    return !stop.stopRequested();
}

void SimClock::wakeAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
}
}
