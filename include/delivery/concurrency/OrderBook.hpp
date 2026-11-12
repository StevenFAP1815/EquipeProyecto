#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace delivery::concurrency {
class OrderBook {
public:
    explicit OrderBook(std::size_t maxPending);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    bool tryPush(core::Order&& order);

    std::optional<core::Order> waitAndPop(ShutdownToken& stop);

    std::optional<core::Order> popNext(std::int64_t nowMs, std::int64_t agingMs);

    void putBack(core::Order&& order);

    std::vector<core::OrderId> removeExpired(std::int64_t nowMs, std::int64_t acceptTimeoutMs);

    std::optional<std::int64_t> nextDeadlineMs(std::int64_t nowMs,
                                               std::int64_t acceptTimeoutMs,
                                               std::int64_t agingMs) const;

    std::size_t size() const;

    std::size_t capacity() const noexcept;

    std::uint64_t rejectedCount() const;

    void wakeAll();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<core::Order> queue_;
    const std::size_t maxPending_;
    std::uint64_t rejectedCount_ = 0;
};
}
