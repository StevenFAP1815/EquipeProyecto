#include <delivery/sim/Fleet.hpp>

#include <delivery/core/Rng.hpp>

#include <mutex>
#include <string>
#include <utility>

namespace delivery::sim {
namespace {
CourierSettings makeBaseSettings(const infra::Config& cfg) {
    CourierSettings settings;
    settings.bagCapacity = cfg.fleet.bagCapacity;
    settings.speedKmh = cfg.fleet.speedKmh;
    settings.breakdownProbability = cfg.incidents.breakdownProbability;
    return settings;
}
}

Fleet::Fleet(const infra::Config& cfg,
             std::shared_ptr<const core::StreetNetwork> network,
             SimClock& clock,
             concurrency::ShutdownToken& stop,
             concurrency::ChangeSignal& freedSignal,
             infra::EventLog& log)
    : env_{std::move(network), clock, stop, freedSignal, desks_, transferMutex_, log,
           [this](Courier& broken) { return findRescuer(broken); }},
      baseSettings_(makeBaseSettings(cfg)),
      reinforcements_(cfg.reinforcements),
      startNode_(cfg.fleet.startNode),
      seed_(cfg.orders.seed) {
    for (const core::Restaurant& restaurant : cfg.restaurants) {
        desks_.emplace(restaurant.id,
                       std::make_unique<concurrency::PickupDesk>(restaurant.pickupSlots));
    }

    const int n = cfg.fleet.couriers;
    couriers_.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        CourierSettings settings = baseSettings_;
        settings.seed = core::deriveSeed(seed_, 1000 + static_cast<std::uint64_t>(i));

        couriers_.push_back(std::make_unique<Courier>(
            core::CourierId("c" + std::to_string(i)), env_, settings, startNode_));
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

void Fleet::startThreadLocked(Courier* courier) {
    threads_.emplace_back([courier] {
        courier->run();
    });
}

void Fleet::start() {
    std::unique_lock<std::shared_mutex> roster(rosterMutex_);
    threads_.reserve(couriers_.size());
    for (auto& c : couriers_) {
        startThreadLocked(c.get());
    }
}

bool Fleet::addReinforcement(std::size_t waitingOrders) {
    std::unique_lock<std::shared_mutex> roster(rosterMutex_);
    if (!reinforcements_.enabled() || env_.stop.stopRequested()) {
        return false;
    }
    if (waitingOrders < static_cast<std::size_t>(reinforcements_.threshold)) {
        return false;
    }

    int active = 0;
    for (const auto& c : couriers_) {
        if (c->isAvailable()) {
            return false;
        }
        if (c->isReinforcement() && !c->isRetired()) {
            ++active;
        }
    }
    if (active >= reinforcements_.max) {
        return false;
    }

    const int number = nextReinforcementId_++;
    const core::CourierId id("r" + std::to_string(number));
    CourierSettings settings = baseSettings_;
    settings.isReinforcement = true;
    settings.idleMs = reinforcements_.idleMs;
    settings.seed = core::deriveSeed(seed_, 2000 + static_cast<std::uint64_t>(number));

    env_.log.courierJoined(env_.clock.nowMs(), id);
    couriers_.push_back(std::make_unique<Courier>(id, env_, settings, startNode_));
    startThreadLocked(couriers_.back().get());
    return true;
}

std::vector<Courier*> Fleet::activeCouriers() const {
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::vector<Courier*> active;
    active.reserve(couriers_.size());
    for (const auto& c : couriers_) {
        if (!c->isRetired()) {
            active.push_back(c.get());
        }
    }
    return active;
}

std::size_t Fleet::size() const {
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    return couriers_.size();
}

void Fleet::join() {
    std::vector<std::thread> toJoin;
    std::vector<Courier*> everyone;
    {
        std::unique_lock<std::shared_mutex> roster(rosterMutex_);
        toJoin = std::move(threads_);
        threads_.clear();
        for (auto& c : couriers_) {
            everyone.push_back(c.get());
        }
    }

    for (Courier* c : everyone) {
        c->wake();
    }
    for (auto& entry : desks_) {
        entry.second->wakeAll();
    }
    for (auto& t : toJoin) {
        if (t.joinable()) {
            t.join();
        }
    }
}

std::optional<std::future<bool>> Fleet::findRescuer(Courier& broken) {
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
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
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->deliveredCount();
    }
    return total;
}

std::uint64_t Fleet::failedCount() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->failedCount();
    }
    return total;
}

std::uint64_t Fleet::ordersInProgress() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->ordersInBag();
    }
    return total;
}

std::uint64_t Fleet::breakdownCount() const {
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->breakdownCount();
    }
    return total;
}

std::uint64_t Fleet::reassignedCount() const {
    std::shared_lock<std::shared_mutex> snapshot(transferMutex_);
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    std::uint64_t total = 0;
    for (const auto& c : couriers_) {
        total += c->reassignedCount();
    }
    return total;
}

Courier* Fleet::findCourier(const core::CourierId& id) {
    std::shared_lock<std::shared_mutex> roster(rosterMutex_);
    for (auto& c : couriers_) {
        if (c->id() == id) {
            return c.get();
        }
    }
    return nullptr;
}
}
