#pragma once

#include <delivery/core/StreetNetwork.hpp>

#include <cstdint>
#include <vector>

namespace delivery::core {
struct CsrGraph {
    int n = 0;
    std::vector<int> rowStart;
    std::vector<int> target;
    std::vector<std::int32_t> weightCm;

    static CsrGraph fromNetwork(const StreetNetwork& network);
    static CsrGraph syntheticGrid(int side, std::uint64_t seed);
};
}
