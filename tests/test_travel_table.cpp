#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/FloydWarshall.hpp>
#include <delivery/infra/ConfigLoader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using namespace delivery;
using core::CsrGraph;
using core::TravelTable;

namespace {
CsrGraph smallGraphWithOneWayAndIsland() {
    const std::vector<core::Node> nodes = {
        {core::NodeId("a"), -17.7600, -63.2000}, {core::NodeId("b"), -17.7600, -63.1990},
        {core::NodeId("c"), -17.7610, -63.1990}, {core::NodeId("d"), -17.7700, -63.1800},
    };
    const std::vector<core::Street> streets = {
        {core::StreetId("s0"), core::NodeId("a"), core::NodeId("b"), false},
        {core::StreetId("s1"), core::NodeId("b"), core::NodeId("c"), true},
        {core::StreetId("s2"), core::NodeId("c"), core::NodeId("a"), true},
    };
    return CsrGraph::fromNetwork(*core::StreetNetwork::build(nodes, streets));
}
}

TEST_CASE("TravelTable: Dijkstra respeta los sentidos y marca lo inalcanzable") {
    const CsrGraph g = smallGraphWithOneWayAndIsland();
    const TravelTable table = core::allPairsDijkstra(g, 1);
    const int a = 0, b = 1, c = 2, d = 3;

    REQUIRE(table.centimeters(a, a) == 0);
    REQUIRE(table.centimeters(b, c) > 0);
    REQUIRE(table.centimeters(c, b) == table.centimeters(c, a) + table.centimeters(a, b));
    REQUIRE(table.centimeters(a, c) == table.centimeters(a, b) + table.centimeters(b, c));
    REQUIRE_FALSE(table.reachable(a, d));
    REQUIRE_FALSE(table.reachable(d, a));
    REQUIRE(table.reachable(d, d));
}

TEST_CASE("TravelTable: la versión multihilo da la misma tabla que la secuencial") {
    const CsrGraph g = CsrGraph::syntheticGrid(12, 7);
    REQUIRE(core::allPairsDijkstra(g, 4).data() == core::allPairsDijkstra(g, 1).data());
}

TEST_CASE("Floyd-Warshall por bloques: igual a Dijkstra desde cada nodo") {
    for (int side : {5, 7, 9}) {
        const CsrGraph g = CsrGraph::syntheticGrid(side, 11);
        const auto reference = core::allPairsDijkstra(g, 1).data();
        const auto tiled = gpu::floydWarshallTiled(g.n, core::denseAdjacency(g));
        REQUIRE(tiled == reference);
    }
}

TEST_CASE("Floyd-Warshall por bloques: conserva los sentidos y lo inalcanzable") {
    const CsrGraph g = smallGraphWithOneWayAndIsland();
    const auto reference = core::allPairsDijkstra(g, 1).data();
    gpu::KernelTiming timing;
    const auto tiled = gpu::floydWarshallTiled(g.n, core::denseAdjacency(g), &timing);
    REQUIRE(tiled == reference);
    REQUIRE(tiled[0 * 4 + 3] == gpu::kFwInfinity);
    REQUIRE(timing.kernelMs >= 0.0);
}

TEST_CASE("TravelTable: coincide con el buscador de rutas del simulador en Equipetrol") {
    const auto cfg = infra::loadConfigFromFile(std::filesystem::path(CONFIG_DIR) / "equipetrol.json");
    const auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    const CsrGraph g = CsrGraph::fromNetwork(*network);
    const TravelTable table = core::allPairsDijkstra(g, 2);

    for (int i = 0; i < g.n; i += 7) {
        for (int j = 0; j < g.n; j += 5) {
            const auto from = *network->nodeIdAt(static_cast<std::size_t>(i));
            const auto to = *network->nodeIdAt(static_cast<std::size_t>(j));
            const auto route = core::findShortestPath(*network, from, to);
            REQUIRE(route.has_value() == table.reachable(i, j));
            if (route) {
                const double edges = static_cast<double>(route->path.size());
                REQUIRE(std::abs(table.centimeters(i, j) / 100.0 - route->totalMeters) <=
                        0.005 * edges + 1e-9);
            }
        }
    }
}
