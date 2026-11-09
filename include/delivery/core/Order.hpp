#pragma once

#include <delivery/core/Types.hpp>

#include <cstdint>
#include <string>

namespace delivery::core {
enum class OrderState {
    Created,
    Assigned,
    PickedUp,
    Delivered,
    Rejected,
    Pending
};

inline const char* toString(OrderState s) noexcept {
    switch (s) {
        case OrderState::Created:   return "created";
        case OrderState::Assigned:  return "assigned";
        case OrderState::PickedUp:  return "pickedUp";
        case OrderState::Delivered: return "delivered";
        case OrderState::Rejected:  return "rejected";
        case OrderState::Pending:   return "pending";
    }
    return "unknown";
}

inline bool isFinal(OrderState s) noexcept {
    return s == OrderState::Delivered
        || s == OrderState::Rejected
        || s == OrderState::Pending;
}

enum class RejectReason {
    None,
    QueueFull,
    Unreachable,
    NoCourier
};

inline const char* toString(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:        return "none";
        case RejectReason::QueueFull:   return "queueFull";
        case RejectReason::Unreachable: return "unreachable";
        case RejectReason::NoCourier:   return "noCourier";
    }
    return "unknown";
}

struct Order {
    OrderId id;
    RestaurantId restaurant;
    NodeId restaurantNode;
    NodeId deliveryNode;
    std::int64_t createdAtMs = 0;
    std::int64_t readyAtMs = 0;
    OrderState state = OrderState::Created;
    RejectReason rejectReason = RejectReason::None;
    CourierId assignedCourier;
};
}
