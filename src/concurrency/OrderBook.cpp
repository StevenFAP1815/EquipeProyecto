#include <delivery/concurrency/OrderBook.hpp>

#include <utility>

namespace delivery::concurrency {
OrderBook::OrderBook(std::size_t maxPending)
    : maxPending_(maxPending) {}

bool OrderBook::tryPush(core::Order&& order) {
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() < maxPending_) {
            queue_.push_back(std::move(order));
            accepted = true;
        } else {
            ++rejectedCount_;
        }
    }
    if (accepted) {
        cv_.notify_one();
    }
    return accepted;
}

std::optional<core::Order> OrderBook::waitAndPop(ShutdownToken& stop) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this, &stop] {
        return !queue_.empty() || stop.stopRequested();
    });

    if (!queue_.empty()) {
        core::Order order = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        cv_.notify_one();
        return order;
    }

    return std::nullopt;
}

std::size_t OrderBook::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t OrderBook::capacity() const noexcept {
    return maxPending_;
}

std::uint64_t OrderBook::rejectedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rejectedCount_;
}

void OrderBook::wakeAll() noexcept {
    cv_.notify_all();
}
}
