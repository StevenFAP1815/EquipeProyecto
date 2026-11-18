#include <delivery/concurrency/OrderBook.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace delivery::concurrency {
namespace {
bool isUrgentNow(const core::Order& order, std::int64_t nowMs, std::int64_t agingMs) {
    if (order.urgent) {
        return true;
    }
    return agingMs > 0 && nowMs - order.createdAtMs >= agingMs;
}

bool goesBefore(const core::Order& a, const core::Order& b,
                std::int64_t nowMs, std::int64_t agingMs) {
    const bool aUrgent = isUrgentNow(a, nowMs, agingMs);
    const bool bUrgent = isUrgentNow(b, nowMs, agingMs);
    if (aUrgent != bUrgent) {
        return aUrgent;
    }
    if (a.createdAtMs != b.createdAtMs) {
        return a.createdAtMs < b.createdAtMs;
    }
    const std::string& aId = a.id.str();
    const std::string& bId = b.id.str();
    if (aId.size() != bId.size()) {
        return aId.size() < bId.size();
    }
    return aId < bId;
}
}

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

std::optional<core::Order> OrderBook::popNext(std::int64_t nowMs, std::int64_t agingMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < queue_.size(); ++i) {
        if (goesBefore(queue_[i], queue_[best], nowMs, agingMs)) {
            best = i;
        }
    }

    core::Order order = std::move(queue_[best]);
    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(best));
    return order;
}

void OrderBook::putBack(core::Order&& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(order));
}

std::vector<core::OrderId> OrderBook::removeExpired(std::int64_t nowMs,
                                                    std::int64_t acceptTimeoutMs) {
    std::vector<core::OrderId> expired;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queue_.begin();
    while (it != queue_.end()) {
        if (nowMs >= it->createdAtMs + acceptTimeoutMs) {
            expired.push_back(it->id);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

std::optional<std::int64_t> OrderBook::nextDeadlineMs(std::int64_t nowMs,
                                                      std::int64_t acceptTimeoutMs,
                                                      std::int64_t agingMs) const {
    std::optional<std::int64_t> next;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const core::Order& order : queue_) {
        const std::int64_t acceptDeadline = order.createdAtMs + acceptTimeoutMs;
        if (acceptDeadline > nowMs && (!next || acceptDeadline < *next)) {
            next = acceptDeadline;
        }
        if (agingMs > 0 && !order.urgent) {
            const std::int64_t agingDeadline = order.createdAtMs + agingMs;
            if (agingDeadline > nowMs && (!next || agingDeadline < *next)) {
                next = agingDeadline;
            }
        }
    }
    return next;
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

void OrderBook::wakeAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
}
}
