#include <delivery/core/CsrGraph.hpp>

#include <cmath>
#include <random>
#include <tuple>

namespace delivery::core {
namespace {
CsrGraph buildCsr(int n, const std::vector<std::tuple<int, int, std::int32_t>>& edges) {
    CsrGraph g;
    g.n = n;
    g.rowStart.assign(static_cast<std::size_t>(n) + 1, 0);
    for (const auto& [from, to, w] : edges) {
        ++g.rowStart[static_cast<std::size_t>(from) + 1];
    }
    for (int i = 0; i < n; ++i) {
        g.rowStart[static_cast<std::size_t>(i) + 1] += g.rowStart[static_cast<std::size_t>(i)];
    }
    g.target.resize(edges.size());
    g.weightCm.resize(edges.size());
    std::vector<int> next(g.rowStart.begin(), g.rowStart.end() - 1);
    for (const auto& [from, to, w] : edges) {
        const auto slot = static_cast<std::size_t>(next[static_cast<std::size_t>(from)]++);
        g.target[slot] = to;
        g.weightCm[slot] = w;
    }
    return g;
}
}

CsrGraph CsrGraph::fromNetwork(const StreetNetwork& network) {
    const int n = static_cast<int>(network.nodeCount());
    std::vector<std::tuple<int, int, std::int32_t>> edges;
    for (int u = 0; u < n; ++u) {
        const NodeId id = *network.nodeIdAt(static_cast<std::size_t>(u));
        for (const Edge& edge : network.neighbors(id)) {
            const int v = static_cast<int>(*network.nodeIndex(edge.to));
            const auto cm = static_cast<std::int32_t>(std::llround(edge.lengthMeters * 100.0));
            edges.emplace_back(u, v, cm);
        }
    }
    return buildCsr(n, edges);
}

CsrGraph CsrGraph::syntheticGrid(int side, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int32_t> length(10000, 18000);
    std::vector<std::tuple<int, int, std::int32_t>> edges;
    auto id = [side](int r, int c) { return r * side + c; };

    for (int r = 0; r < side; ++r) {
        const bool avenue = r % 4 == 0;
        const bool eastbound = r % 2 == 0;
        for (int c = 0; c + 1 < side; ++c) {
            const std::int32_t w = length(rng);
            if (avenue || eastbound) {
                edges.emplace_back(id(r, c), id(r, c + 1), w);
            }
            if (avenue || !eastbound) {
                edges.emplace_back(id(r, c + 1), id(r, c), w);
            }
        }
    }
    for (int c = 0; c < side; ++c) {
        for (int r = 0; r + 1 < side; ++r) {
            const std::int32_t w = length(rng);
            edges.emplace_back(id(r, c), id(r + 1, c), w);
            edges.emplace_back(id(r + 1, c), id(r, c), w);
        }
    }
    return buildCsr(side * side, edges);
}
}
