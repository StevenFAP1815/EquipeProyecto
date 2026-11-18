#include <delivery/core/TravelTable.hpp>

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>

namespace delivery::core {
TravelTable::TravelTable(int n, std::vector<std::int32_t> centimeters)
    : n_(n), cm_(std::move(centimeters)) {
    if (cm_.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) {
        throw std::invalid_argument("travel table size does not match n*n");
    }
}

std::int32_t TravelTable::centimeters(int from, int to) const {
    return cm_[static_cast<std::size_t>(from) * static_cast<std::size_t>(n_) +
               static_cast<std::size_t>(to)];
}

bool TravelTable::reachable(int from, int to) const {
    return centimeters(from, to) < kUnreachable;
}

std::vector<std::int32_t> denseAdjacency(const CsrGraph& graph) {
    const auto n = static_cast<std::size_t>(graph.n);
    std::vector<std::int32_t> matrix(n * n, TravelTable::kUnreachable);
    for (std::size_t u = 0; u < n; ++u) {
        matrix[u * n + u] = 0;
        for (int e = graph.rowStart[u]; e < graph.rowStart[u + 1]; ++e) {
            const auto v = static_cast<std::size_t>(graph.target[static_cast<std::size_t>(e)]);
            std::int32_t& cell = matrix[u * n + v];
            cell = std::min(cell, graph.weightCm[static_cast<std::size_t>(e)]);
        }
    }
    return matrix;
}

namespace {
void dijkstraFrom(const CsrGraph& graph, int source, std::int32_t* row) {
    using Entry = std::pair<std::int32_t, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    std::fill(row, row + graph.n, TravelTable::kUnreachable);
    row[source] = 0;
    heap.emplace(0, source);
    while (!heap.empty()) {
        const auto [d, u] = heap.top();
        heap.pop();
        if (d > row[u]) {
            continue;
        }
        const auto uIndex = static_cast<std::size_t>(u);
        for (int e = graph.rowStart[uIndex]; e < graph.rowStart[uIndex + 1]; ++e) {
            const auto edge = static_cast<std::size_t>(e);
            const int v = graph.target[edge];
            const std::int32_t candidate = d + graph.weightCm[edge];
            if (candidate < row[v]) {
                row[v] = candidate;
                heap.emplace(candidate, v);
            }
        }
    }
}
}

TravelTable allPairsDijkstra(const CsrGraph& graph, unsigned threads) {
    const auto n = static_cast<std::size_t>(graph.n);
    std::vector<std::int32_t> table(n * n);
    const unsigned workers = std::max(1u, threads);

    auto work = [&graph, &table, n, workers](unsigned worker) {
        for (std::size_t source = worker; source < n; source += workers) {
            dijkstraFrom(graph, static_cast<int>(source), table.data() + source * n);
        }
    };

    if (workers == 1) {
        work(0);
    } else {
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w) {
            pool.emplace_back(work, w);
        }
        for (std::thread& t : pool) {
            t.join();
        }
    }
    return TravelTable(graph.n, std::move(table));
}
}
