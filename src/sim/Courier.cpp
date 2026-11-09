#include <delivery/sim/Courier.hpp>

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
                 double speedKmh,
                 core::NodeId startNode)
    : id_(std::move(id)),
      network_(std::move(network)),
      clock_(clock),
      stop_(stop),
      freedSignal_(freedSignal),
      desks_(desks),
      speedKmh_(speedKmh),
      currentNode_(std::move(startNode)) {
    const auto coords = network_->nodeLatLon(currentNode_);
    if (coords) {
        position_ = *coords;
    }
    lastIdleMs_ = clock_.nowMs();
}

bool Courier::tryAssign(core::Order& order) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != CourierState::Idle || job_.has_value()) {
            return false;
        }
        order.state = core::OrderState::Assigned;
        order.assignedCourier = id_;
        job_ = std::move(order);
        state_ = CourierState::MovingToRestaurant;
    }
    cv_.notify_one();
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
    return state_ == CourierState::Idle && !job_.has_value();
}

core::LatLon Courier::position() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return position_;
}

core::NodeId Courier::currentNode() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentNode_;
}

std::optional<core::NodeId> Courier::activeDeliveryNode() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!job_) {
        return std::nullopt;
    }
    return job_->deliveryNode;
}

bool Courier::hasOrder() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return job_.has_value();
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

void Courier::finishJob(bool delivered) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        job_.reset();
        if (delivered) {
            ++delivered_;
        } else {
            ++failed_;
        }
        state_ = CourierState::Idle;
        lastIdleMs_ = clock_.nowMs();
    }
    freedSignal_.notify();
}

void Courier::run() {
    while (true) {
        core::NodeId from;
        core::RestaurantId restaurant;
        core::NodeId restaurantNode;
        core::NodeId deliveryNode;
        std::int64_t readyAtMs = 0;
        {
            std::unique_lock<std::mutex> lock(stateMutex_);
            cv_.wait(lock, [&] {
                return job_.has_value() || stop_.stopRequested();
            });
            if (stop_.stopRequested()) {
                return;
            }
            from = currentNode_;
            restaurant = job_->restaurant;
            restaurantNode = job_->restaurantNode;
            deliveryNode = job_->deliveryNode;
            readyAtMs = job_->readyAtMs;
        }

        if (!travel(from, restaurantNode)) {
            if (stop_.stopRequested()) {
                return;
            }
            finishJob(false);
            continue;
        }

        setState(CourierState::PickingUp);
        if (!pickUpAtDesk(restaurant, readyAtMs)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            job_->state = core::OrderState::PickedUp;
            state_ = CourierState::Delivering;
        }

        if (!travel(restaurantNode, deliveryNode)) {
            if (stop_.stopRequested()) {
                return;
            }
            finishJob(false);
            continue;
        }

        finishJob(true);
    }
}

bool Courier::pickUpAtDesk(const core::RestaurantId& restaurant, std::int64_t readyAtMs) {
    concurrency::PickupDesk& desk = *desks_.at(restaurant);
    if (!desk.enter(stop_)) {
        return false;
    }

    const bool pickedUp =
        clock_.waitUntilMs(readyAtMs, stop_) &&
        clock_.waitUntilMs(clock_.nowMs() + kPickupHandlingMs, stop_);

    desk.leave();
    return pickedUp;
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
