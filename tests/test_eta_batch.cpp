#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/EtaBatch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

using namespace delivery;

namespace {
constexpr std::int32_t kInf = gpu::kTableInfinity;

std::vector<std::int32_t> threeNodeTable() {
    return {
        0,      100000, 250000,
        100000, 0,      150000,
        kInf,   kInf,   0,
    };
}

gpu::EtaOrders oneOrder(int restaurant, int delivery, long long readyAt) {
    gpu::EtaOrders orders;
    orders.restaurant.push_back(restaurant);
    orders.delivery.push_back(delivery);
    orders.readyAtMs.push_back(readyAt);
    return orders;
}
}

TEST_CASE("ETA por lotes: suma la ida al restaurante, la espera y las entregas de la bolsa") {
    const auto table = threeNodeTable();
    gpu::EtaCouriers couriers;
    couriers.add(0, {}, 0, 1000);
    couriers.add(0, {2}, 0, 1000);
    couriers.add(0, {}, 500000, 1000);

    const auto etas = gpu::etaMatrixCpu(table, 3, oneOrder(1, 0, 0), couriers, 0, 1);
    REQUIRE(etas[0] == 100000 + 100000);
    REQUIRE(etas[1] == gpu::kNoEta);
    REQUIRE(etas[2] == 500000 + 100000);
}

TEST_CASE("ETA por lotes: lo inalcanzable da kNoEta") {
    const auto table = threeNodeTable();
    gpu::EtaCouriers couriers;
    couriers.add(2, {}, 0, 1000);
    const auto etas = gpu::etaMatrixCpu(table, 3, oneOrder(0, 1, 0), couriers, 0, 1);
    REQUIRE(etas[0] == gpu::kNoEta);
}

TEST_CASE("ETA por lotes: la bolsa no admite más paradas que kMaxBagStops") {
    gpu::EtaCouriers couriers;
    REQUIRE_THROWS(couriers.add(0, std::vector<int>(gpu::kMaxBagStops + 1, 0), 0, 1000));
}

TEST_CASE("ETA por lotes: CPU multihilo, kernel SoA y kernel AoS dan la misma matriz") {
    const auto table = core::allPairsDijkstra(core::CsrGraph::syntheticGrid(6, 5), 2);
    std::mt19937_64 rng(9);
    std::uniform_int_distribution<int> node(0, table.size() - 1);
    std::uniform_int_distribution<long long> ready(0, 200000);
    std::uniform_int_distribution<int> bag(0, 3);

    gpu::EtaOrders orders;
    for (int o = 0; o < 3; ++o) {
        orders.restaurant.push_back(node(rng));
        orders.delivery.push_back(node(rng));
        orders.readyAtMs.push_back(ready(rng));
    }
    gpu::EtaCouriers couriers;
    for (int c = 0; c < 300; ++c) {
        std::vector<int> stops;
        const int size = bag(rng);
        for (int k = 0; k < size; ++k) {
            stops.push_back(node(rng));
        }
        couriers.add(node(rng), stops, ready(rng), 833);
    }

    const long long now = 12345;
    const auto reference = gpu::etaMatrixCpu(table.data(), table.size(), orders, couriers, now, 1);
    REQUIRE(gpu::etaMatrixCpu(table.data(), table.size(), orders, couriers, now, 3) == reference);

    gpu::EtaEngine engine(table.size(), table.data());
    REQUIRE(engine.etaMatrixSoA(orders, couriers, now) == reference);
    REQUIRE(engine.etaMatrixAoS(orders, couriers, now) == reference);
}

TEST_CASE("ETA por lotes: un lote vacío devuelve una matriz vacía") {
    const auto table = threeNodeTable();
    gpu::EtaEngine engine(3, table);
    REQUIRE(engine.etaMatrixSoA(gpu::EtaOrders{}, gpu::EtaCouriers{}, 0).empty());
}
