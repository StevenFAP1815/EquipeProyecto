#pragma once

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/SimClock.hpp>

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace delivery::sim {
class Fleet {
public:
    Fleet(const infra::Config& cfg,
          std::shared_ptr<const core::StreetNetwork> network,
          SimClock& clock,
          concurrency::ShutdownToken& stop,
          concurrency::ChangeSignal& freedSignal,
          infra::EventLog& log);

    ~Fleet();

    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    void start();

    void join();

    bool addReinforcement(std::size_t waitingOrders);

    std::vector<Courier*> activeCouriers() const;

    const std::vector<std::unique_ptr<Courier>>& couriers() const noexcept {
        return couriers_;
    }

    std::size_t size() const;

    Courier* findCourier(const core::CourierId& id);

    std::uint64_t deliveredCount() const;
    std::uint64_t failedCount() const;
    std::uint64_t ordersInProgress() const;
    std::uint64_t breakdownCount() const;
    std::uint64_t reassignedCount() const;

    std::shared_mutex& transferMutex() const noexcept { return transferMutex_; }

    const concurrency::PickupDesk& desk(const core::RestaurantId& id) const {
        return *desks_.at(id);
    }

    concurrency::PickupDesk& desk(const core::RestaurantId& id) {
        return *desks_.at(id);
    }

    std::uint64_t pickupsDelayedByPause() const;

private:
    std::optional<std::future<bool>> findRescuer(Courier& broken);
    void startThreadLocked(Courier* courier);

    mutable std::shared_mutex transferMutex_;
    mutable std::shared_mutex rosterMutex_;
    PickupDeskMap desks_;
    const CourierEnvironment env_;
    const CourierSettings baseSettings_;
    const infra::ReinforcementsConfig reinforcements_;
    const core::NodeId startNode_;
    const std::uint64_t seed_;
    int nextReinforcementId_ = 0;
    std::vector<std::unique_ptr<Courier>> couriers_;
    std::vector<std::thread> threads_;
};
}
