#include <delivery/infra/EventLog.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace delivery::infra {
namespace {
using Json = nlohmann::ordered_json;

Json baseEvent(std::int64_t t, const char* name) {
    Json event;
    event["t"] = t;
    event["event"] = name;
    return event;
}
}

std::string toJsonLine(const Summary& summary) {
    Json perCourier = Json::object();
    for (const auto& entry : summary.deliveredPerCourier) {
        perCourier[entry.first] = entry.second;
    }
    Json j;
    j["created"] = summary.created;
    j["delivered"] = summary.delivered;
    j["rejected"] = summary.rejected;
    j["pending"] = summary.pending;
    j["reassigned"] = summary.reassigned;
    j["breakdowns"] = summary.breakdowns;
    j["avgDeliveryMs"] = summary.avgDeliveryMs;
    j["maxDeliveryMs"] = summary.maxDeliveryMs;
    j["deliveredPerCourier"] = perCourier;
    j["reinforcementsJoined"] = summary.reinforcementsJoined;
    j["reinforcementsRetired"] = summary.reinforcementsRetired;
    j["urgentCreated"] = summary.urgentCreated;
    j["urgentDelivered"] = summary.urgentDelivered;
    j["avgUrgentDeliveryMs"] = summary.avgUrgentDeliveryMs;
    j["pauses"] = summary.pauses;
    j["pickupsDelayedByPause"] = summary.pickupsDelayedByPause;

    Json breaksPerCourier = Json::object();
    for (const auto& entry : summary.breaksPerCourier) {
        breaksPerCourier[entry.first] = entry.second;
    }
    j["breaks"] = summary.breaks;
    j["breaksPerCourier"] = breaksPerCourier;
    return j.dump();
}

EventLog::EventLog() = default;

EventLog::EventLog(const std::filesystem::path& path) : file_(path) {
    if (!file_) {
        throw std::runtime_error("cannot open event log: " + path.string());
    }
}

void EventLog::writeLine(const std::string& line) {
    if (file_.is_open()) {
        file_ << line << '\n';
    }
}

void EventLog::registerCouriers(const std::vector<core::CourierId>& couriers) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const core::CourierId& c : couriers) {
        counts_.deliveredPerCourier.emplace(c.str(), 0);
        counts_.breaksPerCourier.emplace(c.str(), 0);
    }
}

void EventLog::orderCreated(std::int64_t t, const core::Order& order) {
    Json e = baseEvent(t, "orderCreated");
    e["order"] = order.id.str();
    e["restaurant"] = order.restaurant.str();
    e["urgent"] = order.urgent;

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.created;
    createdAtByOrder_[order.id.str()] = order.createdAtMs;
    if (order.urgent) {
        ++counts_.urgentCreated;
        urgentOrders_.insert(order.id.str());
    }
}

void EventLog::orderAssigned(std::int64_t t, const core::OrderId& order,
                             const core::CourierId& courier) {
    Json e = baseEvent(t, "orderAssigned");
    e["order"] = order.str();
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
}

void EventLog::pickupStarted(std::int64_t t, const core::OrderId& order,
                             const core::CourierId& courier,
                             const core::RestaurantId& restaurant,
                             std::int64_t queuedAtMs) {
    Json e = baseEvent(t, "pickupStarted");
    e["order"] = order.str();
    e["courier"] = courier.str();
    e["restaurant"] = restaurant.str();
    e["queuedAt"] = queuedAtMs;

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
}

void EventLog::orderPickedUp(std::int64_t t, const core::OrderId& order,
                             const core::CourierId& courier,
                             const core::RestaurantId& restaurant) {
    Json e = baseEvent(t, "orderPickedUp");
    e["order"] = order.str();
    e["courier"] = courier.str();
    e["restaurant"] = restaurant.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
}

void EventLog::orderDelivered(std::int64_t t, const core::OrderId& order,
                              const core::CourierId& courier) {
    Json e = baseEvent(t, "orderDelivered");
    e["order"] = order.str();
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.delivered;
    ++counts_.deliveredPerCourier[courier.str()];

    const auto created = createdAtByOrder_.find(order.str());
    if (created != createdAtByOrder_.end()) {
        const std::int64_t deliveryMs = t - created->second;
        totalDeliveryMs_ += deliveryMs;
        counts_.maxDeliveryMs = std::max(counts_.maxDeliveryMs, deliveryMs);
        createdAtByOrder_.erase(created);

        if (urgentOrders_.erase(order.str()) > 0) {
            ++counts_.urgentDelivered;
            totalUrgentDeliveryMs_ += deliveryMs;
        }
    }
}

void EventLog::orderRejected(std::int64_t t, const core::OrderId& order,
                             core::RejectReason reason) {
    Json e = baseEvent(t, "orderRejected");
    e["order"] = order.str();
    e["reason"] = core::toString(reason);

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.rejected;
    if (reason == core::RejectReason::QueueFull) {
        ++counts_.queueFull;
    } else if (reason == core::RejectReason::Unreachable) {
        ++counts_.unreachable;
    } else if (reason == core::RejectReason::NoCourier) {
        ++counts_.noCourier;
    }
    createdAtByOrder_.erase(order.str());
    urgentOrders_.erase(order.str());
}

void EventLog::courierBreakdown(std::int64_t t, const core::CourierId& courier) {
    Json e = baseEvent(t, "courierBreakdown");
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.breakdowns;
}

void EventLog::orderReassigned(std::int64_t t, const core::OrderId& order,
                               const core::CourierId& from, const core::CourierId& to) {
    Json e = baseEvent(t, "orderReassigned");
    e["order"] = order.str();
    e["from"] = from.str();
    e["to"] = to.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.reassigned;
}

void EventLog::courierJoined(std::int64_t t, const core::CourierId& courier) {
    Json e = baseEvent(t, "courierJoined");
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.reinforcementsJoined;
    counts_.deliveredPerCourier.emplace(courier.str(), 0);
    counts_.breaksPerCourier.emplace(courier.str(), 0);
}

void EventLog::courierRetired(std::int64_t t, const core::CourierId& courier) {
    Json e = baseEvent(t, "courierRetired");
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.reinforcementsRetired;
}

void EventLog::restaurantPaused(std::int64_t t, const core::RestaurantId& restaurant) {
    Json e = baseEvent(t, "restaurantPaused");
    e["restaurant"] = restaurant.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.pauses;
}

void EventLog::restaurantResumed(std::int64_t t, const core::RestaurantId& restaurant) {
    Json e = baseEvent(t, "restaurantResumed");
    e["restaurant"] = restaurant.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
}

void EventLog::courierBreakStarted(std::int64_t t, const core::CourierId& courier) {
    Json e = baseEvent(t, "courierBreakStarted");
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    ++counts_.breaks;
    ++counts_.breaksPerCourier[courier.str()];
}

void EventLog::courierBreakEnded(std::int64_t t, const core::CourierId& courier) {
    Json e = baseEvent(t, "courierBreakEnded");
    e["courier"] = courier.str();

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
}

void EventLog::simulationStopping(std::int64_t t) {
    const Json e = baseEvent(t, "simulationStopping");

    std::lock_guard<std::mutex> lock(mutex_);
    writeLine(e.dump());
    file_.flush();
}

Summary EventLog::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Summary s = counts_;
    s.pending = s.created - s.delivered - s.rejected;
    s.avgDeliveryMs = s.delivered == 0
                          ? 0
                          : totalDeliveryMs_ / static_cast<std::int64_t>(s.delivered);
    s.avgUrgentDeliveryMs =
        s.urgentDelivered == 0
            ? 0
            : totalUrgentDeliveryMs_ / static_cast<std::int64_t>(s.urgentDelivered);
    return s;
}
}
