#pragma once

#include <delivery/core/CityModel.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/Types.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace delivery::infra {
struct MapConfig {
    std::string imagePath;
    std::string attribution;
    delivery::core::GeoBounds bounds;
};

struct FleetConfig {
    int couriers = 1;
    int bagCapacity = 1;
    double speedKmh = 0.0;
    delivery::core::NodeId startNode;
};

struct OrdersConfig {
    int meanIntervalMs = 0;
    int burstMax = 1;
    int maxPending = 0;
    std::uint64_t seed = 0;
    double urgentProbability = 0.0;
    std::int64_t agingMs = 0;
};

struct DispatchConfig {
    int quoteTimeoutMs = 0;
    int acceptTimeoutMs = 0;
};

struct IncidentsConfig {
    double breakdownProbability = 0.0;
};

struct SimulationConfig {
    int durationS = 0;
    double timeScale = 1.0;
};

struct ReinforcementsConfig {
    int max = 0;
    int threshold = 1;
    std::int64_t idleMs = 1;

    bool enabled() const noexcept { return max > 0; }
};

struct Config {
    MapConfig map;
    std::vector<delivery::core::Node> nodes;
    std::vector<delivery::core::Street> streets;
    std::vector<delivery::core::Restaurant> restaurants;
    FleetConfig fleet;
    OrdersConfig orders;
    DispatchConfig dispatch;
    IncidentsConfig incidents;
    SimulationConfig simulation;
    ReinforcementsConfig reinforcements;

    std::unordered_map<delivery::core::NodeId,
                       std::size_t,
                       delivery::core::NodeIdHash> nodeIndex;
    std::unordered_map<delivery::core::StreetId,
                       std::size_t,
                       delivery::core::StreetIdHash> streetIndex;
    std::unordered_map<delivery::core::RestaurantId,
                       std::size_t,
                       delivery::core::RestaurantIdHash> restaurantIndex;
};
}
