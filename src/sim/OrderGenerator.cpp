#include <delivery/sim/OrderGenerator.hpp>

#include <delivery/core/Rng.hpp>

#include <string>
#include <utility>

namespace delivery::sim {
namespace {
constexpr double kBurstProbability = 0.05;
}

OrderGenerator::OrderGenerator(const infra::Config& config,
                               concurrency::OrderBook& book,
                               concurrency::ShutdownToken& stop,
                               SimClock& clock,
                               std::atomic<std::uint64_t>& orderIdCounter)
    : config_(config),
      book_(book),
      stop_(stop),
      clock_(clock),
      orderIdCounter_(orderIdCounter),
      rng_(core::deriveSeed(config.orders.seed, 0)),
      intervalDist_(
          1.0 / static_cast<double>(config.orders.meanIntervalMs)),
      burstSizeDist_(1, config.orders.burstMax),
      burstProbDist_(0.0, 1.0),
      pickRestaurantDist_(0, config.restaurants.empty()
                                ? 0
                                : config.restaurants.size() - 1),
      pickNodeDist_(0, config.nodes.empty() ? 0 : config.nodes.size() - 1) {}

core::OrderId OrderGenerator::nextOrderId() {
    const std::uint64_t n = orderIdCounter_.fetch_add(1, std::memory_order_relaxed);
    return core::OrderId("o" + std::to_string(n));
}

std::optional<core::Order> OrderGenerator::generateOne(std::int64_t nowMs) {
    if (config_.restaurants.empty() || config_.nodes.empty()) {
        return std::nullopt;
    }

    const auto& rest = config_.restaurants[pickRestaurantDist_(rng_)];
    const auto& node = config_.nodes[pickNodeDist_(rng_)];

    std::uniform_int_distribution<int> prepTimeDist(rest.prepTimeMs.minMs,
                                                    rest.prepTimeMs.maxMs);

    core::Order o;
    o.id             = nextOrderId();
    o.restaurant     = rest.id;
    o.restaurantNode = rest.node;
    o.deliveryNode   = node.id;
    o.createdAtMs    = nowMs;
    o.readyAtMs      = nowMs + prepTimeDist(rng_);
    o.state          = core::OrderState::Created;
    return o;
}

void OrderGenerator::run() {
    const std::int64_t durationMs =
        static_cast<std::int64_t>(config_.simulation.durationS) * 1000;
    std::int64_t nextOrderMs = 0;

    while (!stop_.stopRequested()) {
        if (durationMs > 0 && nextOrderMs >= durationMs) {
            break;
        }
        if (!clock_.waitUntilMs(nextOrderMs, stop_)) {
            break;
        }

        int count = 1;
        if (burstProbDist_(rng_) < kBurstProbability) {
            count = burstSizeDist_(rng_);
        }

        for (int i = 0; i < count; ++i) {
            auto maybeOrder = generateOne(nextOrderMs);
            if (!maybeOrder) {
                break;
            }
            book_.tryPush(std::move(*maybeOrder));
        }

        nextOrderMs += static_cast<std::int64_t>(intervalDist_(rng_));
    }
}
}
