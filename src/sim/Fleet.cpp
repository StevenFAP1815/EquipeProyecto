#include <delivery/sim/Fleet.hpp>

#include <delivery/core/Rng.hpp>

#include <mutex>
#include <string>

namespace delivery::sim {
Fleet::Fleet(const infra::Config& cfg,
             std::shared_ptr<const core::StreetNetwork> network,
             SimClock& clock,
             concurrency::ShutdownToken& stop,
             concurrency::ChangeSignal& freedSignal,
             infra::EventLog& log) {
    for (const core::Restaurant& restaurant : cfg.restaurants) {
        desks_.emplace(restaurant.id,
                       std::make_unique<concurrency::PickupDesk>(restaurant.pickupSlots));
    }

    const CourierEnvironment environment{
        network, clock, stop, freedSignal, desks_, transferMutex_, log,
        [this](Courier& broken) { return findRescuer(broken); }};

    const int n = cfg.fleet.couriers;
    couriers_.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        CourierSettings settings;
        settings.bagCapacity = cfg.fleet.bagCapacity;
        settings.speedKmh = cfg.fleet.speedKmh;
        settings.breakdownProbability = cfg.incidents.breakdownProbability;
        settings.seed = core::deriveSeed(cfg.orders.seed, 1000 + static_cast<std::uint64_t>(i));

        couriers_.push_back(std::make_unique<Courier>(
            core::CourierId("c" + std::to_string(i)), environment, settings,
            cfg.fleet.startNode));
    }

    std::vector<core::CourierId> ids;
    for (const auto& c : couriers_) {
        ids.push_back(c->id());
    }
    log.registerCouriers(ids);
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

std::optional<std::future<bool>> Fleet::findRescuer(Courier& broken) {
    for (auto& candidate : couriers_) {
        if (candidate.get() == &broken) {
            continue;
        }
        auto answer = candidate->acceptRescue(broken);
        if (answer) {
            return answer;
        }
    }
    return std::nullopt;
}

std::uint64_t Fleet::deliveredCount() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->deliveredCount();
    }
    return total;
}

std::uint64_t Fleet::failedCount() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->failedCount();
    }
    return total;
}

std::uint64_t Fleet::ordersInProgress() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->ordersInBag();
    }
    return total;
}

std::uint64_t Fleet::breakdownCount() const {
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->breakdownCount();
    }
    return total;
}

std::uint64_t Fleet::reassignedCount() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->reassignedCount();
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
