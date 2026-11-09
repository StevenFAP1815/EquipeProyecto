#include <delivery/sim/OrderGenerator.hpp>

#include <delivery/core/Rng.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace delivery::sim {
namespace {
constexpr double kBurstProbability = 0.05;
}

OrderGenerator::OrderGenerator(const infra::Config& config,
                               concurrency::OrderBook& book,
                               concurrency::ShutdownToken& stop,
                               std::atomic<std::uint64_t>& orderIdCounter)
    : config_(config),
      book_(book),
      stop_(stop),
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

    core::Order o;
    o.id           = nextOrderId();
    o.restaurant   = rest.id;
    o.deliveryNode = node.id;
    o.createdAtMs  = nowMs;
    o.state        = core::OrderState::Created;
    return o;
}

bool OrderGenerator::sleepUntilNextTick(std::int64_t& simulatedNowMs) {
    const double simulatedSleepMs = intervalDist_(rng_);

    const double realSleepUs =
        simulatedSleepMs * 1000.0 / config_.simulation.timeScale;

    constexpr double kMaxSliceUs = 50'000.0;
    double remainingUs = realSleepUs;

    while (remainingUs > 0.0) {
        if (stop_.stopRequested()) {
            return false;
        }
        const double sliceUs = std::min(remainingUs, kMaxSliceUs);
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<std::int64_t>(sliceUs)));
        remainingUs -= sliceUs;
    }

    simulatedNowMs += static_cast<std::int64_t>(simulatedSleepMs);
    return !stop_.stopRequested();
}

void OrderGenerator::run() {
    std::int64_t simulatedNowMs = 0;
    const std::int64_t durationMs =
        static_cast<std::int64_t>(config_.simulation.durationS) * 1000;

    while (!stop_.stopRequested()) {
        if (durationMs > 0 && simulatedNowMs >= durationMs) {
            break;
        }

        int count = 1;
        if (burstProbDist_(rng_) < kBurstProbability) {
            count = burstSizeDist_(rng_);
        }

        for (int i = 0; i < count && !stop_.stopRequested(); ++i) {
            auto maybeOrder = generateOne(simulatedNowMs);
            if (!maybeOrder) {
                break;
            }
            (void)book_.tryPush(std::move(*maybeOrder));
        }

        if (!sleepUntilNextTick(simulatedNowMs)) {
            break;
        }
    }
}
}
