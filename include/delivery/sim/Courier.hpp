#pragma once

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/PickupDesk.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>
#include <delivery/sim/SimClock.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace delivery::sim {
using core::CourierId;

using PickupDeskMap = std::unordered_map<core::RestaurantId,
                                         std::unique_ptr<concurrency::PickupDesk>,
                                         core::RestaurantIdHash>;
enum class CourierState {
    Idle,
    MovingToRestaurant,
    PickingUp,
    Delivering,
    BrokenDown
};

inline const char* toString(CourierState s) noexcept {
    switch (s) {
        case CourierState::Idle:               return "idle";
        case CourierState::MovingToRestaurant: return "movingToRestaurant";
        case CourierState::PickingUp:          return "pickingUp";
        case CourierState::Delivering:         return "delivering";
        case CourierState::BrokenDown:         return "brokenDown";
    }
    return "unknown";
}

class Courier {
public:
    Courier(CourierId id,
            std::shared_ptr<const core::StreetNetwork> network,
            SimClock& clock,
            concurrency::ShutdownToken& stop,
            concurrency::ChangeSignal& freedSignal,
            const PickupDeskMap& desks,
            int bagCapacity,
            double speedKmh,
            core::NodeId startNode);

    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    void run();

    bool tryAssign(core::Order& order);

    void wake();

    CourierId id() const noexcept { return id_; }
    double speedKmh() const noexcept { return speedKmh_; }
    std::size_t bagCapacity() const noexcept { return bagCapacity_; }
    CourierState state() const;
    bool isAvailable() const;
    bool canAccept(const core::RestaurantId& restaurant) const;
    core::LatLon position() const;
    core::NodeId currentNode() const;
    std::vector<core::NodeId> deliveryNodesInBag() const;
    std::int64_t latestReadyAtMs() const;
    std::size_t ordersInBag() const;
    std::uint64_t deliveredCount() const;
    std::uint64_t failedCount() const;
    std::int64_t lastIdleMs() const;

private:
    bool canAcceptLocked(const core::RestaurantId& restaurant) const;
    std::int64_t latestReadyAtLocked() const;
    bool pickUpAtDesk(const core::RestaurantId& restaurant);
    bool waitUntilBagIsReady();
    void markBagPickedUp();
    bool deliverBag();
    void failWholeBag();
    void becomeIdle();
    bool travel(const core::NodeId& from, const core::NodeId& to);
    bool doMovement(const core::RouteResult& route);
    void setPosition(const core::LatLon& pos);
    void setState(CourierState state);

    const CourierId id_;
    std::shared_ptr<const core::StreetNetwork> network_;
    SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    concurrency::ChangeSignal& freedSignal_;
    const PickupDeskMap& desks_;
    const std::size_t bagCapacity_;
    const double speedKmh_;

    mutable std::mutex stateMutex_;
    std::condition_variable cv_;
    CourierState state_ = CourierState::Idle;
    core::LatLon position_;
    core::NodeId currentNode_;
    std::vector<core::Order> bag_;
    core::RestaurantId bagRestaurant_;
    bool collecting_ = false;
    std::uint64_t delivered_ = 0;
    std::uint64_t failed_ = 0;
    std::int64_t lastIdleMs_ = 0;
};
}
