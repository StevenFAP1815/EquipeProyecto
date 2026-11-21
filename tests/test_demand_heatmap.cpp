#include <delivery/gpu/DemandHeatmap.hpp>
#include <delivery/infra/EventLog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <random>
#include <vector>

using namespace delivery;

TEST_CASE("Mapa de calor: el filtro gaussiano 5x5 suma 256") {
    const auto weights = gpu::heatFilterWeights();
    REQUIRE(weights.size() == 25);
    REQUIRE(std::accumulate(weights.begin(), weights.end(), 0) == gpu::kHeatWeightSum);
    REQUIRE(weights[12] == 36);
}

TEST_CASE("Mapa de calor: un pedido aislado se reparte con los pesos del filtro") {
    const int side = 9;
    std::vector<int> counts(side * side, 0);
    counts[4 * side + 4] = 1;
    const auto smoothed = gpu::smoothDemandCpu(counts, side, side, 1);
    REQUIRE(smoothed[4 * side + 4] == 36);
    REQUIRE(smoothed[4 * side + 5] == 24);
    REQUIRE(smoothed[2 * side + 2] == 1);
    REQUIRE(smoothed[0] == 0);
    REQUIRE(std::accumulate(smoothed.begin(), smoothed.end(), 0) == gpu::kHeatWeightSum);
}

TEST_CASE("Mapa de calor: en el borde se rellena con ceros") {
    std::vector<int> counts(4 * 4, 0);
    counts[0] = 1;
    const auto smoothed = gpu::smoothDemandCpu(counts, 4, 4, 1);
    REQUIRE(smoothed[0] == 36);
    REQUIRE(std::accumulate(smoothed.begin(), smoothed.end(), 0) == (6 + 4 + 1) * (6 + 4 + 1));
}

TEST_CASE("Mapa de calor: CPU multihilo y kernel por bloques con halo coinciden exactamente") {
    std::mt19937_64 rng(3);
    std::poisson_distribution<int> orders(1.5);
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{1, 1}, {16, 16}, {17, 5}, {40, 33}}) {
        std::vector<int> counts(static_cast<std::size_t>(width) * height);
        for (int& c : counts) {
            c = orders(rng);
        }
        const auto reference = gpu::smoothDemandCpu(counts, width, height, 1);
        REQUIRE(gpu::smoothDemandCpu(counts, width, height, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::smoothDemand(counts, width, height, &timing) == reference);
    }
}

TEST_CASE("EventLog: recuerda el punto de entrega de cada pedido creado") {
    infra::EventLog log;
    core::Order order;
    order.id = core::OrderId("o5");
    order.restaurant = core::RestaurantId("r0");
    order.deliveryNode = core::NodeId("n42");
    log.orderCreated(0, order);
    REQUIRE(log.createdDeliveryNodes() == std::vector<std::string>{"n42"});
}
