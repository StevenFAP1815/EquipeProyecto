#include <delivery/concurrency/ShutdownToken.hpp>

namespace delivery::concurrency {

void ShutdownToken::requestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    // Notificar DESPUES de liberar el lock (regla de oro para evitar
    // wakeups innecesarios y posibles deadlocks).
    cv_.notify_all();
}

bool ShutdownToken::stopRequested() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_;
}

void ShutdownToken::waitForStop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return stop_; });
}

}  // namespace delivery::concurrency