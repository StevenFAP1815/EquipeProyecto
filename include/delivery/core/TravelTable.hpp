#pragma once

#include <delivery/core/CsrGraph.hpp>

#include <cstdint>
#include <vector>

namespace delivery::core {
class TravelTable {
public:
    static constexpr std::int32_t kUnreachable = 0x3fffffff;

    TravelTable(int n, std::vector<std::int32_t> centimeters);

    int size() const noexcept { return n_; }
    std::int32_t centimeters(int from, int to) const;
    bool reachable(int from, int to) const;
    const std::vector<std::int32_t>& data() const noexcept { return cm_; }

private:
    int n_;
    std::vector<std::int32_t> cm_;
};

std::vector<std::int32_t> denseAdjacency(const CsrGraph& graph);
TravelTable allPairsDijkstra(const CsrGraph& graph, unsigned threads);
}
