#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>
#include <delivery/sim/SimClock.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace delivery::sim {
using core::CourierId;
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
            double speedKmh,
            core::NodeId startNode);

    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    void run();

    bool setTarget(const core::NodeId& target);

    void wake();

    CourierId id() const noexcept { return id_; }
    CourierState state() const;
    core::LatLon position() const;

    std::int64_t lastIdleMs() const;

private:
    void doMovement(const core::RouteResult& route);

    void setPosition(const core::LatLon& pos);

    const CourierId id_;
    std::shared_ptr<const core::StreetNetwork> network_;
    SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    const double speedKmh_;
    const core::NodeId startNode_;

    mutable std::mutex stateMutex_;
    std::condition_variable cv_;
    CourierState state_ = CourierState::Idle;
    core::LatLon position_;
    std::optional<core::NodeId> pendingTarget_;
    std::int64_t lastIdleMs_ = 0;
};
}
