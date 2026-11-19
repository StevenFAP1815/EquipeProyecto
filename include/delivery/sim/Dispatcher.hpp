#pragma once

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/GpuWorker.hpp>
#include <delivery/core/PlanarProjection.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/EtaBatch.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace delivery::sim {
struct QuoteRequest {
    core::RestaurantId restaurant;
    core::NodeId restaurantNode;
    core::NodeId deliveryNode;
    std::int64_t readyAtMs = 0;
};

class Dispatcher {
public:
    using Estimator = std::function<std::int64_t(const Courier&, QuoteRequest)>;

    Dispatcher(const infra::Config& config,
               std::shared_ptr<const core::StreetNetwork> network,
               concurrency::OrderBook& book,
               Fleet& fleet,
               SimClock& clock,
               concurrency::ChangeSignal& freedSignal,
               concurrency::ShutdownToken& stop,
               infra::EventLog& log,
               Estimator estimator = nullptr);

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    static constexpr std::int32_t kCandidateRadiusDm = 20000;
    static constexpr std::size_t kGpuFilterMinCouriers = 64000;
    static constexpr std::size_t kGpuEtaMinPairs = 10000;

    static bool gpuFilterWorthIt(std::size_t couriers) { return couriers >= kGpuFilterMinCouriers; }
    static bool gpuEtaWorthIt(std::size_t orders, std::size_t couriers) {
        return orders * couriers >= kGpuEtaMinPairs;
    }

    void useTravelTable(std::shared_ptr<const core::TravelTable> table);
    void useGpu(concurrency::GpuWorker& worker);
    void useEtaEngine(gpu::EtaEngine* engine);

    void run();

    std::uint64_t unreachableCount() const { return unreachable_.load(); }
    std::uint64_t noCourierCount() const { return noCourier_.load(); }
    std::uint64_t quoteErrorCount() const { return quoteErrors_.load(); }
    std::uint64_t lateQuoteCount() const { return lateQuotes_.load(); }
    std::uint64_t pendingAtStopCount() const { return pendingAtStop_.load(); }
    std::size_t maxCandidates() const noexcept { return maxCandidates_; }

private:
    bool isReachable(const core::Order& order) const;
    void dispatch(core::Order& order);
    Courier* chooseCourier(const QuoteRequest& request);
    std::vector<Courier*> nearestCandidates(const QuoteRequest& request) const;
    std::vector<Courier*> filteredCandidates(const QuoteRequest& request) const;
    Courier* chooseCourierBatch(const QuoteRequest& request);
    gpu::BestCourier batchBest(const gpu::EtaOrders& orders, const gpu::EtaCouriers& couriers,
                               long long nowMs);
    int nodeIndexOf(const core::NodeId& node) const;
    static int cmPerSecond(double speedKmh);
    std::vector<int> filterCandidates(const std::vector<std::int32_t>& xDm,
                                      const std::vector<std::int32_t>& yDm,
                                      const std::vector<std::uint8_t>& available,
                                      core::PlanarPoint center) const;
    std::int64_t estimateDeliveryMs(const Courier& courier, QuoteRequest request) const;
    std::optional<double> travelMeters(const core::NodeId& from, const core::NodeId& to) const;

    const infra::Config& config_;
    std::shared_ptr<const core::StreetNetwork> network_;
    std::shared_ptr<const core::TravelTable> travelTable_;
    concurrency::GpuWorker* gpuWorker_ = nullptr;
    gpu::EtaEngine* etaEngine_ = nullptr;
    core::PlanarProjection projection_;
    concurrency::OrderBook& book_;
    Fleet& fleet_;
    SimClock& clock_;
    concurrency::ChangeSignal& freedSignal_;
    concurrency::ShutdownToken& stop_;
    infra::EventLog& log_;
    Estimator estimator_;
    const bool customEstimator_;
    const std::size_t maxCandidates_;

    std::atomic<std::uint64_t> unreachable_{0};
    std::atomic<std::uint64_t> noCourier_{0};
    std::atomic<std::uint64_t> quoteErrors_{0};
    std::atomic<std::uint64_t> lateQuotes_{0};
    std::atomic<std::uint64_t> pendingAtStop_{0};
};
}
