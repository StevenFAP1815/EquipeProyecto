#pragma once

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/PickupDesk.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/SimClock.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
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

class Courier;

using RescueRequester = std::function<std::optional<std::future<bool>>(Courier& broken)>;

struct CourierEnvironment {
    std::shared_ptr<const core::StreetNetwork> network;
    SimClock& clock;
    concurrency::ShutdownToken& stop;
    concurrency::ChangeSignal& freedSignal;
    const PickupDeskMap& desks;
    std::shared_mutex& transferMutex;
    infra::EventLog& log;
    RescueRequester requestRescue;
};

struct CourierSettings {
    int bagCapacity = 1;
    double speedKmh = 30.0;
    double breakdownProbability = 0.0;
    std::uint64_t seed = 0;
};

class Courier {
public:
    Courier(CourierId id,
            CourierEnvironment environment,
            CourierSettings settings,
            core::NodeId startNode);

    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    void run();

    bool tryAssign(core::Order& order);

    std::optional<std::future<bool>> acceptRescue(Courier& broken);

    void wake();

    CourierId id() const noexcept { return id_; }
    double speedKmh() const noexcept { return settings_.speedKmh; }
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
    std::uint64_t breakdownCount() const;
    std::uint64_t reassignedCount() const;
    std::int64_t lastIdleMs() const;

private:
    struct RescueRequest {
        Courier* broken = nullptr;
        std::promise<bool> done;
    };

    bool canAcceptLocked(const core::RestaurantId& restaurant) const;
    std::int64_t latestReadyAtLocked() const;
    bool pickUpAtDesk(const core::RestaurantId& restaurant);
    bool waitUntilBagIsReady();
    void markBagPickedUp();
    bool deliverBag(bool mayBreakDown);
    bool breakDownAndWaitForRescue();
    void performRescue(RescueRequest request);
    void failWholeBag();
    void becomeIdle();
    bool travel(const core::NodeId& from, const core::NodeId& to);
    bool travelHalfway(const core::NodeId& from, const core::NodeId& to);
    bool doMovement(const core::RouteResult& route);
    void setPosition(const core::LatLon& pos);
    void setState(CourierState state);

    const CourierId id_;
    const CourierEnvironment env_;
    const CourierSettings settings_;
    const std::size_t bagCapacity_;

    std::mt19937_64 breakdownRng_;
    std::bernoulli_distribution breakdownDist_;

    mutable std::mutex stateMutex_;
    std::condition_variable cv_;
    CourierState state_ = CourierState::Idle;
    core::LatLon position_;
    core::NodeId currentNode_;
    std::vector<core::Order> bag_;
    core::RestaurantId bagRestaurant_;
    bool collecting_ = false;
    bool atDesk_ = false;
    std::optional<RescueRequest> rescueRequest_;
    std::uint64_t delivered_ = 0;
    std::uint64_t failed_ = 0;
    std::uint64_t breakdowns_ = 0;
    std::uint64_t reassigned_ = 0;
    std::int64_t lastIdleMs_ = 0;
};
}
