#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/ArgminReduction.hpp>
#include <delivery/gpu/EtaBatch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

using namespace delivery;

namespace {
constexpr long long kNo = gpu::kNoEta;
}

TEST_CASE("Argmin por pedido: en un empate gana el índice menor") {
    const std::vector<long long> etas = {
        500, 300, 300, 900,
        kNo, kNo, kNo, kNo,
        7,   kNo, 7,   1,
    };
    const auto best = gpu::rowArgminCpu(etas, 3, 4, 1);
    REQUIRE(best[0] == gpu::BestCourier{1, 300});
    REQUIRE(best[1].index == -1);
    REQUIRE(best[2] == gpu::BestCourier{3, 1});
    REQUIRE(gpu::rowArgmin(etas, 3, 4) == best);
}

TEST_CASE("Argmin por pedido: el kernel coincide con la CPU con muchos empates") {
    std::mt19937_64 rng(21);
    std::uniform_int_distribution<long long> eta(100, 104);
    std::bernoulli_distribution unreachable(0.1);
    for (int cols : {1, 255, 256, 257, 1500}) {
        const int rows = 4;
        std::vector<long long> etas(static_cast<std::size_t>(rows) * cols);
        for (long long& v : etas) {
            v = unreachable(rng) ? kNo : eta(rng);
        }
        const auto reference = gpu::rowArgminCpu(etas, rows, cols, 1);
        REQUIRE(gpu::rowArgminCpu(etas, rows, cols, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::rowArgmin(etas, rows, cols, &timing) == reference);
    }
}

TEST_CASE("Argmin por pedido: ETA y argmin en la GPU eligen igual que la CPU") {
    const auto table = core::allPairsDijkstra(core::CsrGraph::syntheticGrid(6, 4), 2);
    std::mt19937_64 rng(5);
    std::uniform_int_distribution<int> node(0, table.size() - 1);
    gpu::EtaOrders orders;
    for (int o = 0; o < 2; ++o) {
        orders.restaurant.push_back(node(rng));
        orders.delivery.push_back(node(rng));
        orders.readyAtMs.push_back(0);
    }
    gpu::EtaCouriers couriers;
    for (int c = 0; c < 260; ++c) {
        couriers.add(node(rng), {}, 0, 833);
    }
    const auto etas = gpu::etaMatrixCpu(table.data(), table.size(), orders, couriers, 0, 1);
    const auto expected = gpu::rowArgminCpu(etas, orders.size(), couriers.size(), 1);

    gpu::EtaEngine engine(table.size(), table.data());
    REQUIRE(engine.bestPerOrder(orders, couriers, 0) == expected);
}
