#include <delivery/core/PathFinder.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace delivery::core {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

// Entrada del heap: par (distancia, indice interno).
// El heap es un min-heap por distancia.
using HeapEntry = std::pair<double, std::size_t>;

struct HeapGreater {
    bool operator()(const HeapEntry& a, const HeapEntry& b) const noexcept {
        return a.first > b.first;  // std::priority_queue es max-heap por defecto
    }
};

}  // namespace

std::optional<RouteResult> findShortestPath(const StreetNetwork& network,
                                            const NodeId& from,
                                            const NodeId& to) {
    const auto fromIdxOpt = network.nodeIndex(from);
    const auto toIdxOpt   = network.nodeIndex(to);
    if (!fromIdxOpt || !toIdxOpt) {
        return std::nullopt;
    }

    const std::size_t fromIdx = *fromIdxOpt;
    const std::size_t toIdx   = *toIdxOpt;

    // Caso trivial: from == to.
    if (fromIdx == toIdx) {
        return RouteResult{{from}, 0.0};
    }

    const std::size_t n = network.nodeCount();

    std::vector<double> dist(n, kInfinity);
    std::vector<std::size_t> prev(n, std::numeric_limits<std::size_t>::max());

    dist[fromIdx] = 0.0;

    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapGreater> pq;
    pq.emplace(0.0, fromIdx);

    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();

        if (d > dist[u]) continue;  // entrada obsoleta
        if (u == toIdx) break;      // encontramos el destino

        const NodeId uId = *network.nodeIdAt(u);
        for (const Edge& e : network.neighbors(uId)) {
            const auto vIdxOpt = network.nodeIndex(e.to);
            if (!vIdxOpt) continue;
            const std::size_t v = *vIdxOpt;

            const double nd = d + e.lengthMeters;
            if (nd < dist[v]) {
                dist[v] = nd;
                prev[v] = u;
                pq.emplace(nd, v);
            }
        }
    }

    if (dist[toIdx] == kInfinity) {
        return std::nullopt;
    }

    // Reconstruir la ruta hacia atras.
    std::vector<NodeId> path;
    std::size_t cur = toIdx;
    while (cur != std::numeric_limits<std::size_t>::max()) {
        path.push_back(*network.nodeIdAt(cur));
        if (cur == fromIdx) break;
        cur = prev[cur];
    }
    std::reverse(path.begin(), path.end());

    return RouteResult{std::move(path), dist[toIdx]};
}

}  // namespace delivery::core