#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/ConfigLoader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <queue>
#include <vector>

using namespace delivery;

namespace {
std::size_t reachableCount(const std::vector<std::vector<std::size_t>>& graph, std::size_t start) {
    std::vector<bool> seen(graph.size(), false);
    std::queue<std::size_t> pending;
    seen[start] = true;
    pending.push(start);
    std::size_t count = 0;
    while (!pending.empty()) {
        const std::size_t u = pending.front();
        pending.pop();
        ++count;
        for (std::size_t v : graph[u]) {
            if (!seen[v]) {
                seen[v] = true;
                pending.push(v);
            }
        }
    }
    return count;
}
}

TEST_CASE("Ciudad: la configuración tiene la escala de una ciudad") {
    const auto cfg = infra::loadConfigFromFile(std::filesystem::path(CONFIG_DIR) / "city.json");
    REQUIRE(cfg.nodes.size() >= 3000);
    REQUIRE(cfg.nodes.size() <= 5000);
    REQUIRE(cfg.fleet.couriers >= 2000);
    REQUIRE(cfg.fleet.couriers <= 5000);
    REQUIRE(cfg.orders.maxPending >= 500);
}

TEST_CASE("Ciudad: el grafo de calles es fuertemente conexo respetando los sentidos") {
    const auto cfg = infra::loadConfigFromFile(std::filesystem::path(CONFIG_DIR) / "city.json");
    const auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    const std::size_t n = network->nodeCount();

    std::vector<std::vector<std::size_t>> forward(n);
    std::vector<std::vector<std::size_t>> backward(n);
    for (std::size_t u = 0; u < n; ++u) {
        const core::NodeId id = *network->nodeIdAt(u);
        for (const core::Edge& edge : network->neighbors(id)) {
            const std::size_t v = *network->nodeIndex(edge.to);
            forward[u].push_back(v);
            backward[v].push_back(u);
        }
    }

    const std::size_t start = *network->nodeIndex(cfg.fleet.startNode);
    REQUIRE(reachableCount(forward, start) == n);
    REQUIRE(reachableCount(backward, start) == n);
}
