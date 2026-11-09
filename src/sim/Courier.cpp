#include <delivery/sim/Courier.hpp>

#include <algorithm>
#include <utility>

namespace delivery::sim {
namespace {
constexpr int kStepsPerSegment = 20;
constexpr std::int64_t kPickupHandlingMs = 30'000;
}

Courier::Courier(core::CourierId id,
                 std::shared_ptr<const core::StreetNetwork> network,
                 SimClock& clock,
                 concurrency::ShutdownToken& stop,
                 concurrency::ChangeSignal& freedSignal,
                 const PickupDeskMap& desks,
                 int bagCapacity,
                 double speedKmh,
                 core::NodeId startNode)
    : id_(std::move(id)),
      network_(std::move(network)),
      clock_(clock),
      stop_(stop),
      freedSignal_(freedSignal),
      desks_(desks),
      bagCapacity_(static_cast<std::size_t>(bagCapacity)),
      speedKmh_(speedKmh),
      currentNode_(std::move(startNode)) {
    const auto coords = network_->nodeLatLon(currentNode_);
    if (coords) {
        position_ = *coords;
    }
    lastIdleMs_ = clock_.nowMs();
}

bool Courier::canAcceptLocked(const core::RestaurantId& restaurant) const {
    if (bag_.empty()) {
        return state_ == CourierState::Idle;
    }
    return collecting_ && restaurant == bagRestaurant_ && bag_.size() < bagCapacity_;
}

bool Courier::tryAssign(core::Order& order) {
    bool startsTrip = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!canAcceptLocked(order.restaurant)) {
            return false;
        }
        startsTrip = bag_.empty();
        if (startsTrip) {
            bagRestaurant_ = order.restaurant;
            collecting_ = true;
            state_ = CourierState::MovingToRestaurant;
        }
        order.state = core::OrderState::Assigned;
        order.assignedCourier = id_;
        bag_.push_back(std::move(order));
    }
    if (startsTrip) {
        cv_.notify_one();
    }
    return true;
}

void Courier::wake() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
    }
    cv_.notify_all();
}

CourierState Courier::state() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

bool Courier::isAvailable() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_ == CourierState::Idle && bag_.empty();
}

bool Courier::canAccept(const core::RestaurantId& restaurant) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return canAcceptLocked(restaurant);
}

core::LatLon Courier::position() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return position_;
}

core::NodeId Courier::currentNode() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentNode_;
}

std::vector<core::NodeId> Courier::deliveryNodesInBag() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::vector<core::NodeId> nodes;
    for (const core::Order& order : bag_) {
        nodes.push_back(order.deliveryNode);
    }
    return nodes;
}

std::int64_t Courier::latestReadyAtLocked() const {
    std::int64_t latest = 0;
    for (const core::Order& order : bag_) {
        latest = std::max(latest, order.readyAtMs);
    }
    return latest;
}

std::int64_t Courier::latestReadyAtMs() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return latestReadyAtLocked();
}

std::size_t Courier::ordersInBag() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return bag_.size();
}

std::uint64_t Courier::deliveredCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return delivered_;
}

std::uint64_t Courier::failedCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return failed_;
}

std::int64_t Courier::lastIdleMs() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastIdleMs_;
}

void Courier::setPosition(const core::LatLon& pos) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    position_ = pos;
}

void Courier::setState(CourierState state) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_ = state;
}

void Courier::run() {
    while (true) {
        core::NodeId from;
        core::RestaurantId restaurant;
        core::NodeId restaurantNode;
        {
            std::unique_lock<std::mutex> lock(stateMutex_);
            cv_.wait(lock, [&] {
                return !bag_.empty() || stop_.stopRequested();
            });
            if (stop_.stopRequested()) {
                return;
            }
            from = currentNode_;
            restaurant = bagRestaurant_;
            restaurantNode = bag_.front().restaurantNode;
        }

        if (!travel(from, restaurantNode)) {
            if (stop_.stopRequested()) {
                return;
            }
            failWholeBag();
            continue;
        }

        setState(CourierState::PickingUp);
        if (!pickUpAtDesk(restaurant)) {
            return;
        }
        markBagPickedUp();

        if (!deliverBag()) {
            return;
        }
        becomeIdle();
    }
}

bool Courier::pickUpAtDesk(const core::RestaurantId& restaurant) {
    concurrency::PickupDesk& desk = *desks_.at(restaurant);
    if (!desk.enter(stop_)) {
        return false;
    }

    const bool pickedUp =
        waitUntilBagIsReady() &&
        clock_.waitUntilMs(clock_.nowMs() + kPickupHandlingMs, stop_);

    desk.leave();
    return pickedUp;
}

bool Courier::waitUntilBagIsReady() {
    while (true) {
        std::int64_t readyAt = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            readyAt = latestReadyAtLocked();
        }
        if (!clock_.waitUntilMs(readyAt, stop_)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (latestReadyAtLocked() <= clock_.nowMs()) {
            collecting_ = false;
            return true;
        }
    }
}

void Courier::markBagPickedUp() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (core::Order& order : bag_) {
        order.state = core::OrderState::PickedUp;
    }
    state_ = CourierState::Delivering;
}

bool Courier::deliverBag() {
    while (true) {
        core::NodeId target;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (bag_.empty()) {
                return true;
            }
            target = bag_.front().deliveryNode;
        }

        const bool arrived = travel(currentNode(), target);
        if (!arrived && stop_.stopRequested()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        bag_.erase(bag_.begin());
        if (arrived) {
            ++delivered_;
        } else {
            ++failed_;
        }
    }
}

void Courier::failWholeBag() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        failed_ += bag_.size();
        bag_.clear();
        collecting_ = false;
        state_ = CourierState::Idle;
        lastIdleMs_ = clock_.nowMs();
    }
    freedSignal_.notify();
}

void Courier::becomeIdle() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        collecting_ = false;
        state_ = CourierState::Idle;
        lastIdleMs_ = clock_.nowMs();
    }
    freedSignal_.notify();
}

bool Courier::travel(const core::NodeId& from, const core::NodeId& to) {
    const auto route = core::findShortestPath(*network_, from, to);
    if (!route) {
        return false;
    }
    return doMovement(*route);
}

bool Courier::doMovement(const core::RouteResult& route) {
    const double speedMetersPerMs = speedKmh_ / 3600.0;

    for (std::size_t i = 0; i + 1 < route.path.size(); ++i) {
        const auto a = network_->nodeLatLon(route.path[i]);
        const auto b = network_->nodeLatLon(route.path[i + 1]);
        const auto edgeLen = network_->edgeLength(route.path[i], route.path[i + 1]);
        if (!a || !b || !edgeLen) {
            return false;
        }

        const double travelMs = *edgeLen / speedMetersPerMs;
        const std::int64_t t0 = clock_.nowMs();

        for (int s = 1; s <= kStepsPerSegment; ++s) {
            const double alpha = static_cast<double>(s) / kStepsPerSegment;
            const std::int64_t targetMs =
                t0 + static_cast<std::int64_t>(travelMs * alpha);
            if (!clock_.waitUntilMs(targetMs, stop_)) {
                return false;
            }

            core::LatLon p;
            p.lat = a->lat + (b->lat - a->lat) * alpha;
            p.lon = a->lon + (b->lon - a->lon) * alpha;
            setPosition(p);
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        currentNode_ = route.path[i + 1];
    }
    return true;
}
}
