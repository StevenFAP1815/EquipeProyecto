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
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

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
            double speedKmh,
            core::NodeId startNode);

    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    void run();

    bool tryAssign(core::Order& order);

    void wake();

    CourierId id() const noexcept { return id_; }
    double speedKmh() const noexcept { return speedKmh_; }
    CourierState state() const;
    bool isAvailable() const;
    core::LatLon position() const;
    core::NodeId currentNode() const;
    std::optional<core::NodeId> activeDeliveryNode() const;
    bool hasOrder() const;
    std::uint64_t deliveredCount() const;
    std::uint64_t failedCount() const;
    std::int64_t lastIdleMs() const;

private:
    bool pickUpAtDesk(const core::RestaurantId& restaurant, std::int64_t readyAtMs);
    bool travel(const core::NodeId& from, const core::NodeId& to);
    bool doMovement(const core::RouteResult& route);
    void setPosition(const core::LatLon& pos);
    void setState(CourierState state);
    void finishJob(bool delivered);

    const CourierId id_;
    std::shared_ptr<const core::StreetNetwork> network_;
    SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    concurrency::ChangeSignal& freedSignal_;
    const PickupDeskMap& desks_;
    const double speedKmh_;

    mutable std::mutex stateMutex_;
    std::condition_variable cv_;
    CourierState state_ = CourierState::Idle;
    core::LatLon position_;
    core::NodeId currentNode_;
    std::optional<core::Order> job_;
    std::uint64_t delivered_ = 0;
    std::uint64_t failed_ = 0;
    std::int64_t lastIdleMs_ = 0;
};
}
