#include <delivery/sim/Dispatcher.hpp>

#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/PathFinder.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace delivery::sim {
namespace {
std::size_t chooseMaxCandidates() {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return std::max<std::size_t>(2, hardwareThreads);
}
}

Dispatcher::Dispatcher(const infra::Config& config,
                       std::shared_ptr<const core::StreetNetwork> network,
                       concurrency::OrderBook& book,
                       Fleet& fleet,
                       SimClock& clock,
                       concurrency::ChangeSignal& freedSignal,
                       concurrency::ShutdownToken& stop,
                       infra::EventLog& log,
                       Estimator estimator)
    : config_(config),
      network_(std::move(network)),
      book_(book),
      fleet_(fleet),
      clock_(clock),
      freedSignal_(freedSignal),
      stop_(stop),
      log_(log),
      estimator_(std::move(estimator)),
      maxCandidates_(chooseMaxCandidates()) {
    if (!estimator_) {
        estimator_ = [this](const Courier& courier, QuoteRequest request) {
            return estimateDeliveryMs(courier, std::move(request));
        };
    }
}

void Dispatcher::run() {
    const std::int64_t acceptTimeoutMs = config_.dispatch.acceptTimeoutMs;
    const std::int64_t agingMs = config_.orders.agingMs;

    while (!stop_.stopRequested()) {
        const std::uint64_t seenVersion = freedSignal_.version();

        std::optional<core::Order> order = book_.popNext(clock_.nowMs(), agingMs);
        if (!order) {
            freedSignal_.waitForChange(seenVersion, stop_);
            continue;
        }
        if (!isReachable(*order)) {
            ++unreachable_;
            log_.orderRejected(clock_.nowMs(), order->id, core::RejectReason::Unreachable);
            continue;
        }
        if (tryToAssign(*order)) {
            continue;
        }

        book_.putBack(std::move(*order));
        if (stop_.stopRequested()) {
            break;
        }

        const std::int64_t now = clock_.nowMs();
        for (const core::OrderId& id : book_.removeExpired(now, acceptTimeoutMs)) {
            ++noCourier_;
            log_.orderRejected(now, id, core::RejectReason::NoCourier);
        }

        const std::optional<std::int64_t> deadline =
            book_.nextDeadlineMs(now, acceptTimeoutMs, agingMs);
        if (deadline) {
            freedSignal_.waitForChange(seenVersion, clock_.realTimeAt(*deadline), stop_);
        } else {
            freedSignal_.waitForChange(seenVersion, stop_);
        }
    }
}

bool Dispatcher::isReachable(const core::Order& order) const {
    const core::NodeId& start = config_.fleet.startNode;
    return core::findShortestPath(*network_, start, order.restaurantNode) &&
           core::findShortestPath(*network_, order.restaurantNode, order.deliveryNode) &&
           core::findShortestPath(*network_, order.deliveryNode, start);
}

bool Dispatcher::tryToAssign(core::Order& order) {
    const QuoteRequest request{order.restaurant, order.restaurantNode, order.deliveryNode,
                               order.readyAtMs};

    while (!stop_.stopRequested()) {
        Courier* best = chooseCourier(request);
        if (best != nullptr) {
            if (best->tryAssign(order)) {
                return true;
            }
            continue;
        }

        const std::size_t waitingOrders = book_.size() + 1;
        if (!fleet_.addReinforcement(waitingOrders)) {
            return false;
        }
    }
    return false;
}

Courier* Dispatcher::chooseCourier(const QuoteRequest& request) {
    const std::vector<Courier*> candidates = nearestCandidates(request);

    std::vector<std::future<std::int64_t>> quotes;
    for (Courier* courier : candidates) {
        quotes.push_back(std::async(std::launch::async, [this, courier, request] {
            return estimator_(*courier, request);
        }));
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(config_.dispatch.quoteTimeoutMs);

    Courier* best = nullptr;
    std::int64_t bestMs = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        if (quotes[i].wait_until(deadline) != std::future_status::ready) {
            ++lateQuotes_;
            continue;
        }
        try {
            const std::int64_t estimateMs = quotes[i].get();
            if (estimateMs < bestMs) {
                bestMs = estimateMs;
                best = candidates[i];
            }
        } catch (const std::exception&) {
            ++quoteErrors_;
        }
    }
    return best;
}

std::vector<Courier*> Dispatcher::nearestCandidates(const QuoteRequest& request) const {
    const core::LatLon restaurant = *network_->nodeLatLon(request.restaurantNode);

    std::vector<std::pair<double, Courier*>> free;
    for (Courier* courier : fleet_.activeCouriers()) {
        if (courier->canAccept(request.restaurant)) {
            const double meters =
                core::GeoBounds::distanceMeters(courier->position(), restaurant);
            free.emplace_back(meters, courier);
        }
    }

    std::sort(free.begin(), free.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (free.size() > maxCandidates_) {
        free.resize(maxCandidates_);
    }

    std::vector<Courier*> result;
    for (const auto& entry : free) {
        result.push_back(entry.second);
    }
    return result;
}

std::int64_t Dispatcher::estimateDeliveryMs(const Courier& courier, QuoteRequest request) const {
    const auto toRestaurant =
        core::findShortestPath(*network_, courier.currentNode(), request.restaurantNode);
    if (!toRestaurant) {
        throw std::runtime_error("courier " + courier.id().str() +
                                 " cannot reach restaurant node " +
                                 request.restaurantNode.str());
    }
    double metersAfterPickup = 0.0;
    core::NodeId at = request.restaurantNode;
    std::vector<core::NodeId> stops = courier.deliveryNodesInBag();
    stops.push_back(request.deliveryNode);
    for (const core::NodeId& stop : stops) {
        const auto leg = core::findShortestPath(*network_, at, stop);
        if (!leg) {
            throw std::runtime_error("no route between deliveries");
        }
        metersAfterPickup += leg->totalMeters;
        at = stop;
    }

    const double metersPerMs = courier.speedKmh() / 3600.0;
    const std::int64_t now = clock_.nowMs();
    const std::int64_t arrival =
        now + static_cast<std::int64_t>(toRestaurant->totalMeters / metersPerMs);
    const std::int64_t pickup =
        std::max({arrival, request.readyAtMs, courier.latestReadyAtMs()});
    const std::int64_t delivered =
        pickup + static_cast<std::int64_t>(metersAfterPickup / metersPerMs);
    return delivered - now;
}
}
