#include <delivery/sim/KitchenPauses.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace delivery::sim {
KitchenPauses::KitchenPauses(const infra::Config& config,
                             Fleet& fleet,
                             SimClock& clock,
                             concurrency::ShutdownToken& stop,
                             infra::EventLog& log)
    : config_(config),
      fleet_(fleet),
      clock_(clock),
      stop_(stop),
      log_(log) {}

void KitchenPauses::run() {
    if (!config_.kitchen.enabled() || config_.restaurants.empty()) {
        return;
    }

    const std::int64_t every = config_.kitchen.pauseEveryMs;
    const std::int64_t length = config_.kitchen.pauseMs;
    const std::size_t count = config_.restaurants.size();
    const std::int64_t total = static_cast<std::int64_t>(count);

    std::vector<std::int64_t> pauseStart;
    std::vector<bool> paused;
    for (std::size_t i = 0; i < count; ++i) {
        pauseStart.push_back(static_cast<std::int64_t>(i) * every / total);
        paused.push_back(false);
    }

    while (!stop_.stopRequested()) {
        std::size_t next = 0;
        std::int64_t nextMs = 0;
        for (std::size_t i = 0; i < count; ++i) {
            std::int64_t ms = pauseStart[i];
            if (paused[i]) {
                ms = pauseStart[i] + length;
            }
            if (i == 0 || ms < nextMs) {
                next = i;
                nextMs = ms;
            }
        }

        if (!clock_.waitUntilMs(nextMs, stop_)) {
            return;
        }

        const core::RestaurantId& restaurant = config_.restaurants[next].id;
        concurrency::PickupDesk& desk = fleet_.desk(restaurant);
        if (paused[next]) {
            log_.restaurantResumed(nextMs, restaurant);
            desk.resume();
            paused[next] = false;
            pauseStart[next] += every;
        } else {
            desk.pause();
            log_.restaurantPaused(nextMs, restaurant);
            paused[next] = true;
        }
    }
}
}
