#pragma once

#include <delivery/core/CityModel.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/Types.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace delivery::infra {

// ---------------------------------------------------------------------------
// Sub-configuraciones, espejo del JSON de la seccion 5 del proyecto.
// Cada struct agrupa las propiedades de una seccion del JSON.
// ---------------------------------------------------------------------------

struct MapConfig {
    std::string imagePath;      // map.image   (ruta relativa al archivo JSON)
    std::string attribution;    // map.attribution
    delivery::core::GeoBounds bounds;  // map.bounds
};

struct FleetConfig {
    int couriers = 1;                       // fleet.couriers      (>= 1)
    int bagCapacity = 1;                    // fleet.bagCapacity   (>= 1)
    double speedKmh = 0.0;                  // fleet.speedKmh
    delivery::core::NodeId startNode;       // fleet.startNode
};

struct OrdersConfig {
    int meanIntervalMs = 0;   // orders.meanIntervalMs
    int burstMax = 1;         // orders.burstMax (>= 1)
    int maxPending = 0;       // orders.maxPending
    std::uint64_t seed = 0;   // orders.seed (JSON number; puede ser grande)
};

struct DispatchConfig {
    int quoteTimeoutMs = 0;    // dispatch.quoteTimeoutMs
    int acceptTimeoutMs = 0;   // dispatch.acceptTimeoutMs
};

struct IncidentsConfig {
    double breakdownProbability = 0.0;  // incidents.breakdownProbability (0..1)
};

struct SimulationConfig {
    int durationS = 0;          // simulation.durationS (0 = hasta senal)
    double timeScale = 1.0;     // simulation.timeScale
};

// ---------------------------------------------------------------------------
// Config completa del programa.
//
// Incluye los datos crudos del JSON mas indices auxiliares construidos por el
// ConfigLoader tras validar unicidad de IDs. Los indices aceleran las busquedas
// de id -> posicion en el vector correspondiente durante toda la simulacion.
// ---------------------------------------------------------------------------

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

    // Indices auxiliares (construidos por ConfigLoader).
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

}  // namespace delivery::infra