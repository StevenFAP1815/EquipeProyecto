#include <delivery/core/CityModel.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/infra/Config.hpp>

#include <iostream>
#include <unordered_map>

int main() {
    using namespace delivery::core;
    using namespace delivery::infra;

    // Construimos una Config minima a mano (sin JSON todavia).
    Config cfg;

    cfg.map.imagePath   = "data/equipetrol.png";
    cfg.map.attribution = "(c) OpenStreetMap contributors";
    cfg.map.bounds      = GeoBounds(-17.7500, -17.7700, -63.2000, -63.1800);

    cfg.nodes.push_back(Node{NodeId("n0"), -17.7550, -63.1950});
    cfg.nodes.push_back(Node{NodeId("n1"), -17.7600, -63.1900});

    cfg.streets.push_back(Street{StreetId("s0"),
                                 NodeId("n0"), NodeId("n1"),
                                 /*oneWay=*/false});

    cfg.restaurants.push_back(Restaurant{
        RestaurantId("r0"),
        "Example",
        NodeId("n0"),
        /*pickupSlots=*/1,
        TimeRange{60000, 240000}
    });

    cfg.fleet.couriers    = 8;
    cfg.fleet.bagCapacity = 3;
    cfg.fleet.speedKmh    = 30.0;
    cfg.fleet.startNode   = NodeId("n0");

    cfg.orders.meanIntervalMs = 20000;
    cfg.orders.burstMax       = 12;
    cfg.orders.maxPending     = 50;
    cfg.orders.seed           = 42;

    cfg.dispatch.quoteTimeoutMs  = 200;
    cfg.dispatch.acceptTimeoutMs = 600000;

    cfg.incidents.breakdownProbability = 0.02;

    cfg.simulation.durationS = 3600;
    cfg.simulation.timeScale = 60.0;

    // Rellenamos los indices auxiliares a mano (en el loader sera automatico).
    for (std::size_t i = 0; i < cfg.nodes.size(); ++i) {
        cfg.nodeIndex.emplace(cfg.nodes[i].id, i);
    }
    for (std::size_t i = 0; i < cfg.streets.size(); ++i) {
        cfg.streetIndex.emplace(cfg.streets[i].id, i);
    }
    for (std::size_t i = 0; i < cfg.restaurants.size(); ++i) {
        cfg.restaurantIndex.emplace(cfg.restaurants[i].id, i);
    }

    // Sanity check: buscar por id.
    const auto it = cfg.nodeIndex.find(NodeId("n1"));
    if (it == cfg.nodeIndex.end()) {
        std::cerr << "ERROR: n1 no encontrado en nodeIndex\n";
        return 1;
    }
    const Node& n1 = cfg.nodes[it->second];
    std::cout << "n1 -> lat=" << n1.lat << " lon=" << n1.lon << "\n";

    std::cout << "nodes="       << cfg.nodes.size()
              << " streets="    << cfg.streets.size()
              << " restaurants="<< cfg.restaurants.size()
              << " couriers="   << cfg.fleet.couriers
              << "\n";

    std::cout << "Config OK\n";
    return 0;
}