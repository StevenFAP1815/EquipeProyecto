#pragma once

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/infra/Config.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <random>

namespace delivery::sim {
class OrderGenerator {
public:
    OrderGenerator(const infra::Config& config,
                   concurrency::OrderBook& book,
                   concurrency::ShutdownToken& stop,
                   std::atomic<std::uint64_t>& orderIdCounter);

    OrderGenerator(const OrderGenerator&) = delete;
    OrderGenerator& operator=(const OrderGenerator&) = delete;

    void run();

private:
    std::optional<core::Order> generateOne(std::int64_t nowMs);

    bool sleepUntilNextTick(std::int64_t& simulatedNowMs);

    core::OrderId nextOrderId();

    const infra::Config& config_;
    concurrency::OrderBook& book_;
    concurrency::ShutdownToken& stop_;
    std::atomic<std::uint64_t>& orderIdCounter_;

    std::mt19937_64 rng_;
    std::exponential_distribution<double> intervalDist_;
    std::uniform_int_distribution<int> burstSizeDist_;
    std::uniform_real_distribution<double> burstProbDist_;
    std::uniform_int_distribution<std::size_t> pickRestaurantDist_;
    std::uniform_int_distribution<std::size_t> pickNodeDist_;
};
}
