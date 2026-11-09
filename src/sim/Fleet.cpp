#include <delivery/sim/Fleet.hpp>

#include <string>

namespace delivery::sim {
Fleet::Fleet(const infra::Config& cfg,
             std::shared_ptr<const core::StreetNetwork> network,
             SimClock& clock,
             concurrency::ShutdownToken& stop,
             concurrency::ChangeSignal& freedSignal) {
    for (const core::Restaurant& restaurant : cfg.restaurants) {
        desks_.emplace(restaurant.id,
                       std::make_unique<concurrency::PickupDesk>(restaurant.pickupSlots));
    }

    const int n = cfg.fleet.couriers;
    couriers_.reserve(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        const core::CourierId id("c" + std::to_string(i));
        couriers_.push_back(std::make_unique<Courier>(
            id,
            network,
            clock,
            stop,
            freedSignal,
            desks_,
            cfg.fleet.speedKmh,
            cfg.fleet.startNode));
    }
}

Fleet::~Fleet() {
    join();
}

void Fleet::start() {
    threads_.reserve(couriers_.size());
    for (auto& c : couriers_) {
        Courier* ptr = c.get();
        threads_.emplace_back([ptr] {
            ptr->run();
        });
    }
}

void Fleet::join() {
    for (auto& c : couriers_) {
        c->wake();
    }
    for (auto& entry : desks_) {
        entry.second->wakeAll();
    }
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

std::uint64_t Fleet::deliveredCount() const {
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->deliveredCount();
    }
    return total;
}

std::uint64_t Fleet::failedCount() const {
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->failedCount();
    }
    return total;
}

std::uint64_t Fleet::ordersInProgress() const {
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        if (c->hasOrder()) {
            ++total;
        }
    }
    return total;
}

Courier* Fleet::findCourier(const core::CourierId& id) noexcept {
    for (auto& c : couriers_) {
        if (c->id() == id) {
            return c.get();
        }
    }
    return nullptr;
}
}
