#include <delivery/sim/Courier.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

namespace delivery::sim {
namespace {
constexpr int kStepsPerSegment = 20;
constexpr std::int64_t kPickupHandlingMs = 30'000;
constexpr std::int64_t kRepairMs = 300'000;
}

Courier::Courier(core::CourierId id,
                 CourierEnvironment environment,
                 CourierSettings settings,
                 core::NodeId startNode)
    : id_(std::move(id)),
      env_(std::move(environment)),
      settings_(settings),
      bagCapacity_(static_cast<std::size_t>(settings.bagCapacity)),
      breakdownRng_(settings.seed),
      breakdownDist_(settings.breakdownProbability),
      currentNode_(std::move(startNode)) {
    const auto coords = env_.network->nodeLatLon(currentNode_);
    if (coords) {
        position_ = *coords;
    }
    lastIdleMs_ = env_.clock.nowMs();
}

bool Courier::canAcceptLocked(const core::RestaurantId& restaurant) const {
    if (retired_) {
        return false;
    }
    if (bag_.empty()) {
        return state_ == CourierState::Idle && !rescueRequest_.has_value();
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
        const std::int64_t now = env_.clock.nowMs();
        env_.log.orderAssigned(now, order.id, id_);
        if (atDesk_) {
            env_.log.pickupStarted(now, order.id, id_, order.restaurant);
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

std::optional<std::future<bool>> Courier::acceptRescue(Courier& broken) {
    std::future<bool> answer;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (&broken == this || env_.stop.stopRequested() ||
            state_ != CourierState::Idle || !bag_.empty() || rescueRequest_.has_value() ||
            retired_) {
            return std::nullopt;
        }
        rescueRequest_.emplace();
        rescueRequest_->broken = &broken;
        answer = rescueRequest_->done.get_future();
        state_ = CourierState::MovingToRestaurant;
    }
    cv_.notify_one();
    return answer;
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
    return !retired_ && state_ == CourierState::Idle && bag_.empty() &&
           !rescueRequest_.has_value();
}

bool Courier::isRetired() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return retired_;
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

std::uint64_t Courier::ordersHandled() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return bag_.size() + delivered_ + failed_;
}

std::uint64_t Courier::deliveredCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return delivered_;
}

std::uint64_t Courier::failedCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return failed_;
}

std::uint64_t Courier::breakdownCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return breakdowns_;
}

std::uint64_t Courier::reassignedCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return reassigned_;
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
        std::optional<RescueRequest> rescue;
        core::NodeId from;
        core::RestaurantId restaurant;
        core::NodeId restaurantNode;
        {
            std::unique_lock<std::mutex> lock(stateMutex_);
            if (!waitForWorkOrRetire(lock)) {
                return;
            }
            if (env_.stop.stopRequested()) {
                if (rescueRequest_) {
                    rescueRequest_->done.set_value(false);
                    rescueRequest_.reset();
                }
                return;
            }
            if (rescueRequest_) {
                rescue = std::move(rescueRequest_);
                rescueRequest_.reset();
            } else {
                from = currentNode_;
                restaurant = bagRestaurant_;
                restaurantNode = bag_.front().restaurantNode;
            }
        }

        if (rescue) {
            performRescue(std::move(*rescue));
            continue;
        }

        if (!travel(from, restaurantNode)) {
            if (env_.stop.stopRequested()) {
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

        if (!deliverBag(true)) {
            return;
        }
        becomeIdle();
    }
}

bool Courier::waitForWorkOrRetire(std::unique_lock<std::mutex>& lock) {
    const auto hasWork = [&] {
        return !bag_.empty() || rescueRequest_.has_value() || env_.stop.stopRequested();
    };
    if (!settings_.isReinforcement) {
        cv_.wait(lock, hasWork);
        return true;
    }
    while (true) {
        const std::int64_t retireAtMs = lastIdleMs_ + settings_.idleMs;
        if (cv_.wait_until(lock, env_.clock.realTimeAt(retireAtMs), hasWork)) {
            return true;
        }
        if (env_.clock.nowMs() < retireAtMs) {
            continue;
        }
        retired_ = true;
        env_.log.courierRetired(env_.clock.nowMs(), id_);
        return false;
    }
}

bool Courier::pickUpAtDesk(const core::RestaurantId& restaurant) {
    concurrency::PickupDesk& desk = *env_.desks.at(restaurant);
    if (!desk.enter(env_.stop)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        atDesk_ = true;
        for (const core::Order& order : bag_) {
            env_.log.pickupStarted(env_.clock.nowMs(), order.id, id_, order.restaurant);
        }
    }

    const bool pickedUp =
        waitUntilBagIsReady() &&
        env_.clock.waitUntilMs(env_.clock.nowMs() + kPickupHandlingMs, env_.stop);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        atDesk_ = false;
    }
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
        if (!env_.clock.waitUntilMs(readyAt, env_.stop)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (latestReadyAtLocked() <= env_.clock.nowMs()) {
            collecting_ = false;
            return true;
        }
    }
}

void Courier::markBagPickedUp() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (core::Order& order : bag_) {
        order.state = core::OrderState::PickedUp;
        env_.log.orderPickedUp(env_.clock.nowMs(), order.id, id_, order.restaurant);
    }
    state_ = CourierState::Delivering;
}

bool Courier::deliverBag(bool mayBreakDown) {
    bool breakdownPending = mayBreakDown && breakdownDist_(breakdownRng_);

    while (true) {
        core::NodeId target;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (bag_.empty()) {
                return true;
            }
            target = bag_.front().deliveryNode;
        }

        if (breakdownPending) {
            if (!travelHalfway(currentNode(), target) && env_.stop.stopRequested()) {
                return false;
            }
            return breakDownAndWaitForRescue();
        }

        const bool arrived = travel(currentNode(), target);
        if (!arrived && env_.stop.stopRequested()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (arrived) {
            env_.log.orderDelivered(env_.clock.nowMs(), bag_.front().id, id_);
            ++delivered_;
        } else {
            env_.log.orderRejected(env_.clock.nowMs(), bag_.front().id,
                                   core::RejectReason::Unreachable);
            ++failed_;
        }
        bag_.erase(bag_.begin());
    }
}

bool Courier::breakDownAndWaitForRescue() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = CourierState::BrokenDown;
        ++breakdowns_;
        env_.log.courierBreakdown(env_.clock.nowMs(), id_);
    }

    const std::int64_t repairedAtMs = env_.clock.nowMs() + kRepairMs;
    bool rescued = false;
    while (!rescued) {
        const std::uint64_t seenVersion = env_.freedSignal.version();
        std::optional<std::future<bool>> answer = env_.requestRescue(*this);
        if (answer) {
            if (!answer->get()) {
                return false;
            }
            rescued = true;
        } else if (env_.clock.nowMs() >= repairedAtMs) {
            break;
        } else {
            env_.freedSignal.waitForChange(seenVersion, env_.clock.realTimeAt(repairedAtMs),
                                           env_.stop);
            if (env_.stop.stopRequested()) {
                return false;
            }
        }
    }

    if (!env_.clock.waitUntilMs(repairedAtMs, env_.stop)) {
        return false;
    }
    if (rescued) {
        return true;
    }
    setState(CourierState::Delivering);
    return deliverBag(false);
}

void Courier::performRescue(RescueRequest request) {
    Courier& broken = *request.broken;
    core::NodeId meetingPoint;
    {
        std::unique_lock<std::shared_mutex> transferLock(env_.transferMutex);
        std::scoped_lock bothBags(stateMutex_, broken.stateMutex_);
        const std::size_t moved = broken.bag_.size();
        for (core::Order& order : broken.bag_) {
            env_.log.orderReassigned(env_.clock.nowMs(), order.id, broken.id_, id_);
            order.assignedCourier = id_;
            bag_.push_back(std::move(order));
        }
        broken.bag_.clear();
        broken.collecting_ = false;
        reassigned_ += moved;
        meetingPoint = broken.currentNode_;
    }
    request.done.set_value(true);

    if (!travel(currentNode(), meetingPoint)) {
        if (env_.stop.stopRequested()) {
            return;
        }
        failWholeBag();
        return;
    }
    setState(CourierState::Delivering);
    if (!deliverBag(false)) {
        return;
    }
    becomeIdle();
}

void Courier::failWholeBag() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const core::Order& order : bag_) {
            env_.log.orderRejected(env_.clock.nowMs(), order.id, core::RejectReason::Unreachable);
        }
        failed_ += bag_.size();
        bag_.clear();
        collecting_ = false;
        state_ = CourierState::Idle;
        lastIdleMs_ = env_.clock.nowMs();
    }
    env_.freedSignal.notify();
}

void Courier::becomeIdle() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        collecting_ = false;
        state_ = CourierState::Idle;
        lastIdleMs_ = env_.clock.nowMs();
    }
    env_.freedSignal.notify();
}

bool Courier::travel(const core::NodeId& from, const core::NodeId& to) {
    const auto route = core::findShortestPath(*env_.network, from, to);
    if (!route) {
        return false;
    }
    return doMovement(*route);
}

bool Courier::travelHalfway(const core::NodeId& from, const core::NodeId& to) {
    auto route = core::findShortestPath(*env_.network, from, to);
    if (!route) {
        return false;
    }
    route->path.resize(route->path.size() / 2 + 1);
    return doMovement(*route);
}

bool Courier::doMovement(const core::RouteResult& route) {
    const double speedMetersPerMs = settings_.speedKmh / 3600.0;

    for (std::size_t i = 0; i + 1 < route.path.size(); ++i) {
        const auto a = env_.network->nodeLatLon(route.path[i]);
        const auto b = env_.network->nodeLatLon(route.path[i + 1]);
        const auto edgeLen = env_.network->edgeLength(route.path[i], route.path[i + 1]);
        if (!a || !b || !edgeLen) {
            return false;
        }

        const double travelMs = *edgeLen / speedMetersPerMs;
        const std::int64_t t0 = env_.clock.nowMs();

        for (int s = 1; s <= kStepsPerSegment; ++s) {
            const double alpha = static_cast<double>(s) / kStepsPerSegment;
            const std::int64_t targetMs =
                t0 + static_cast<std::int64_t>(travelMs * alpha);
            if (!env_.clock.waitUntilMs(targetMs, env_.stop)) {
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
