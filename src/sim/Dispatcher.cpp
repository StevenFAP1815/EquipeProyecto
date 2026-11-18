#include <delivery/sim/Dispatcher.hpp>

#include <delivery/core/CandidateFilter.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/gpu/CandidateFilter.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
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
      projection_(core::LatLon{(config.map.bounds.north() + config.map.bounds.south()) / 2.0,
                               (config.map.bounds.west() + config.map.bounds.east()) / 2.0}),
      book_(book),
      fleet_(fleet),
      clock_(clock),
      freedSignal_(freedSignal),
      stop_(stop),
      log_(log),
      estimator_(std::move(estimator)),
      customEstimator_(static_cast<bool>(estimator_)),
      maxCandidates_(chooseMaxCandidates()) {
    if (!estimator_) {
        estimator_ = [this](const Courier& courier, QuoteRequest request) {
            return estimateDeliveryMs(courier, std::move(request));
        };
    }
}

void Dispatcher::useTravelTable(std::shared_ptr<const core::TravelTable> table) {
    travelTable_ = std::move(table);
}

void Dispatcher::useEtaEngine(gpu::EtaEngine* engine) {
    etaEngine_ = engine;
}

int Dispatcher::nodeIndexOf(const core::NodeId& node) const {
    return static_cast<int>(*network_->nodeIndex(node));
}

int Dispatcher::cmPerSecond(double speedKmh) {
    return std::max(1, static_cast<int>(std::lround(speedKmh * 100000.0 / 3600.0)));
}

std::vector<long long> Dispatcher::batchEtas(const gpu::EtaOrders& orders,
                                             const gpu::EtaCouriers& couriers, long long nowMs) {
    if (etaEngine_ != nullptr && gpuWorker_ != nullptr) {
        try {
            auto etas = gpuWorker_->submit([engine = etaEngine_, orders, couriers, nowMs] {
                return engine->etaMatrixSoA(orders, couriers, nowMs);
            });
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(config_.dispatch.quoteTimeoutMs);
            if (etas.wait_until(deadline) == std::future_status::ready) {
                return etas.get();
            }
            ++lateQuotes_;
        } catch (const std::exception&) {
            ++quoteErrors_;
        }
    }
    return gpu::etaMatrixCpu(travelTable_->data(), travelTable_->size(), orders, couriers, nowMs,
                             1);
}

Courier* Dispatcher::chooseCourierBatch(const QuoteRequest& request) {
    const std::vector<Courier*> candidates = filteredCandidates(request);
    if (candidates.empty()) {
        return nullptr;
    }
    gpu::EtaOrders orders;
    orders.restaurant.push_back(nodeIndexOf(request.restaurantNode));
    orders.delivery.push_back(nodeIndexOf(request.deliveryNode));
    orders.readyAtMs.push_back(request.readyAtMs);

    gpu::EtaCouriers couriers;
    for (const Courier* courier : candidates) {
        std::vector<int> stops;
        for (const core::NodeId& stop : courier->deliveryNodesInBag()) {
            stops.push_back(nodeIndexOf(stop));
        }
        couriers.add(nodeIndexOf(courier->currentNode()), stops, courier->latestReadyAtMs(),
                     cmPerSecond(courier->speedKmh()));
    }

    const std::vector<long long> etas = batchEtas(orders, couriers, clock_.nowMs());
    Courier* best = nullptr;
    long long bestEta = gpu::kNoEta;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (etas[i] < bestEta) {
            bestEta = etas[i];
            best = candidates[i];
        }
    }
    return best;
}

std::optional<double> Dispatcher::travelMeters(const core::NodeId& from,
                                               const core::NodeId& to) const {
    if (travelTable_) {
        const auto a = network_->nodeIndex(from);
        const auto b = network_->nodeIndex(to);
        if (!a || !b) {
            return std::nullopt;
        }
        const int i = static_cast<int>(*a);
        const int j = static_cast<int>(*b);
        if (!travelTable_->reachable(i, j)) {
            return std::nullopt;
        }
        return travelTable_->centimeters(i, j) / 100.0;
    }
    const auto route = core::findShortestPath(*network_, from, to);
    if (!route) {
        return std::nullopt;
    }
    return route->totalMeters;
}

void Dispatcher::run() {
    while (auto order = book_.waitAndPop(stop_)) {
        if (stop_.stopRequested()) {
            ++pendingAtStop_;
            continue;
        }
        if (!isReachable(*order)) {
            ++unreachable_;
            log_.orderRejected(clock_.nowMs(), order->id, core::RejectReason::Unreachable);
            continue;
        }
        dispatch(*order);
    }
}

bool Dispatcher::isReachable(const core::Order& order) const {
    const core::NodeId& start = config_.fleet.startNode;
    return travelMeters(start, order.restaurantNode) &&
           travelMeters(order.restaurantNode, order.deliveryNode) &&
           travelMeters(order.deliveryNode, start);
}

void Dispatcher::dispatch(core::Order& order) {
    const QuoteRequest request{order.restaurant, order.restaurantNode, order.deliveryNode,
                               order.readyAtMs};
    const std::int64_t acceptDeadlineMs =
        order.createdAtMs + config_.dispatch.acceptTimeoutMs;

    while (!stop_.stopRequested()) {
        const std::uint64_t seenVersion = freedSignal_.version();

        Courier* best = chooseCourier(request);
        if (best != nullptr && best->tryAssign(order)) {
            return;
        }
        if (best != nullptr) {
            continue;
        }

        if (clock_.nowMs() >= acceptDeadlineMs) {
            ++noCourier_;
            log_.orderRejected(clock_.nowMs(), order.id, core::RejectReason::NoCourier);
            return;
        }
        freedSignal_.waitForChange(seenVersion, clock_.realTimeAt(acceptDeadlineMs), stop_);
    }
    ++pendingAtStop_;
}

Courier* Dispatcher::chooseCourier(const QuoteRequest& request) {
    if (travelTable_ && !customEstimator_ &&
        config_.fleet.bagCapacity <= gpu::kMaxBagStops) {
        return chooseCourierBatch(request);
    }
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

void Dispatcher::useGpu(concurrency::GpuWorker& worker) {
    gpuWorker_ = &worker;
}

std::vector<int> Dispatcher::filterCandidates(const std::vector<std::int32_t>& xDm,
                                              const std::vector<std::int32_t>& yDm,
                                              const std::vector<std::uint8_t>& available,
                                              core::PlanarPoint center) const {
    if (gpuWorker_ != nullptr) {
        try {
            auto inside = gpuWorker_->submit([xDm, yDm, available, center] {
                return gpu::filterWithinRadius(xDm, yDm, available, center.xDm, center.yDm,
                                               kCandidateRadiusDm);
            });
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(config_.dispatch.quoteTimeoutMs);
            if (inside.wait_until(deadline) == std::future_status::ready) {
                return inside.get();
            }
        } catch (const std::exception&) {
        }
    }
    return core::filterWithinRadiusCpu(xDm, yDm, available, center.xDm, center.yDm,
                                       kCandidateRadiusDm, 1);
}

std::vector<Courier*> Dispatcher::nearestCandidates(const QuoteRequest& request) const {
    std::vector<Courier*> result = filteredCandidates(request);
    if (result.size() > maxCandidates_) {
        result.resize(maxCandidates_);
    }
    return result;
}

std::vector<Courier*> Dispatcher::filteredCandidates(const QuoteRequest& request) const {
    const core::LatLon restaurant = *network_->nodeLatLon(request.restaurantNode);
    const auto& couriers = fleet_.couriers();

    std::vector<core::LatLon> positions;
    std::vector<std::int32_t> xDm;
    std::vector<std::int32_t> yDm;
    std::vector<std::uint8_t> available;
    positions.reserve(couriers.size());
    xDm.reserve(couriers.size());
    yDm.reserve(couriers.size());
    available.reserve(couriers.size());
    for (const auto& courier : couriers) {
        const core::LatLon position = courier->position();
        const core::PlanarPoint planar = projection_.toDecimeters(position);
        positions.push_back(position);
        xDm.push_back(planar.xDm);
        yDm.push_back(planar.yDm);
        available.push_back(courier->canAccept(request.restaurant) ? 1 : 0);
    }

    std::vector<int> selected =
        filterCandidates(xDm, yDm, available, projection_.toDecimeters(restaurant));
    if (selected.size() < maxCandidates_) {
        selected.clear();
        for (std::size_t i = 0; i < available.size(); ++i) {
            if (available[i] != 0) {
                selected.push_back(static_cast<int>(i));
            }
        }
    }

    std::vector<std::pair<double, Courier*>> free;
    for (int index : selected) {
        const auto i = static_cast<std::size_t>(index);
        free.emplace_back(core::GeoBounds::distanceMeters(positions[i], restaurant),
                          couriers[i].get());
    }

    std::stable_sort(free.begin(), free.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Courier*> result;
    for (const auto& entry : free) {
        result.push_back(entry.second);
    }
    return result;
}

std::int64_t Dispatcher::estimateDeliveryMs(const Courier& courier, QuoteRequest request) const {
    const auto toRestaurant = travelMeters(courier.currentNode(), request.restaurantNode);
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
        const auto leg = travelMeters(at, stop);
        if (!leg) {
            throw std::runtime_error("no route between deliveries");
        }
        metersAfterPickup += *leg;
        at = stop;
    }

    const double metersPerMs = courier.speedKmh() / 3600.0;
    const std::int64_t now = clock_.nowMs();
    const std::int64_t arrival =
        now + static_cast<std::int64_t>(*toRestaurant / metersPerMs);
    const std::int64_t pickup =
        std::max({arrival, request.readyAtMs, courier.latestReadyAtMs()});
    const std::int64_t delivered =
        pickup + static_cast<std::int64_t>(metersAfterPickup / metersPerMs);
    return delivered - now;
}
}
