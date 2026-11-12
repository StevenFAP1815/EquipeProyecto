#pragma once

#include <delivery/core/Order.hpp>
#include <delivery/core/Types.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace delivery::infra {
struct Summary {
    std::uint64_t created = 0;
    std::uint64_t delivered = 0;
    std::uint64_t rejected = 0;
    std::uint64_t pending = 0;
    std::uint64_t reassigned = 0;
    std::uint64_t breakdowns = 0;
    std::int64_t avgDeliveryMs = 0;
    std::int64_t maxDeliveryMs = 0;
    std::map<std::string, std::uint64_t> deliveredPerCourier;
    std::uint64_t queueFull = 0;
    std::uint64_t unreachable = 0;
    std::uint64_t noCourier = 0;
    std::uint64_t reinforcementsJoined = 0;
    std::uint64_t reinforcementsRetired = 0;
    std::uint64_t urgentCreated = 0;
    std::uint64_t urgentDelivered = 0;
    std::int64_t avgUrgentDeliveryMs = 0;
    std::uint64_t pauses = 0;
    std::uint64_t pickupsDelayedByPause = 0;
};

std::string toJsonLine(const Summary& summary);

class EventLog {
public:
    EventLog();
    explicit EventLog(const std::filesystem::path& path);

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;

    void registerCouriers(const std::vector<core::CourierId>& couriers);

    void orderCreated(std::int64_t t, const core::Order& order);
    void orderAssigned(std::int64_t t, const core::OrderId& order, const core::CourierId& courier);
    void pickupStarted(std::int64_t t, const core::OrderId& order, const core::CourierId& courier,
                       const core::RestaurantId& restaurant, std::int64_t queuedAtMs);
    void orderPickedUp(std::int64_t t, const core::OrderId& order, const core::CourierId& courier,
                       const core::RestaurantId& restaurant);
    void orderDelivered(std::int64_t t, const core::OrderId& order, const core::CourierId& courier);
    void orderRejected(std::int64_t t, const core::OrderId& order, core::RejectReason reason);
    void courierBreakdown(std::int64_t t, const core::CourierId& courier);
    void orderReassigned(std::int64_t t, const core::OrderId& order, const core::CourierId& from,
                         const core::CourierId& to);
    void courierJoined(std::int64_t t, const core::CourierId& courier);
    void courierRetired(std::int64_t t, const core::CourierId& courier);
    void restaurantPaused(std::int64_t t, const core::RestaurantId& restaurant);
    void restaurantResumed(std::int64_t t, const core::RestaurantId& restaurant);
    void simulationStopping(std::int64_t t);

    Summary summary() const;

private:
    void writeLine(const std::string& line);

    mutable std::mutex mutex_;
    std::ofstream file_;
    Summary counts_;
    std::int64_t totalDeliveryMs_ = 0;
    std::unordered_map<std::string, std::int64_t> createdAtByOrder_;
    std::unordered_set<std::string> urgentOrders_;
    std::int64_t totalUrgentDeliveryMs_ = 0;
};
}
