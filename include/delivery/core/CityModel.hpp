#pragma once

#include <delivery/core/Types.hpp>

#include <string>
#include <utility>

namespace delivery::core {
struct TimeRange {
    int minMs = 0;
    int maxMs = 0;
};

struct Node {
    NodeId id;
    double lat = 0.0;
    double lon = 0.0;
};

struct Street {
    StreetId id;
    NodeId from;
    NodeId to;
    bool oneWay = false;
};

struct Restaurant {
    RestaurantId id;
    std::string name;
    NodeId node;
    int pickupSlots = 1;
    TimeRange prepTimeMs;
};
}
