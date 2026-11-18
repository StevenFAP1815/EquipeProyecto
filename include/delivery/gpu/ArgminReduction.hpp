#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kArgminBlock = 256;

struct BestCourier {
    int index = -1;
    long long etaMs = 0x7fffffffffffffffLL;

    bool operator==(const BestCourier& other) const {
        return index == other.index && etaMs == other.etaMs;
    }
};

std::vector<BestCourier> rowArgminCpu(const std::vector<long long>& etas, int rows, int cols,
                                      unsigned threads);
std::vector<BestCourier> rowArgmin(const std::vector<long long>& etas, int rows, int cols,
                                   KernelTiming* timing = nullptr);
void launchRowArgmin(const long long* deviceEtas, int rows, int cols, BestCourier* deviceOut);
}
