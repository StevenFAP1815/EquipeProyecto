#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kResponseTile = 16;
constexpr int kResponseUnreachable = 1 << 30;

std::vector<int> responseMapCpu(const std::vector<int>& initial, const std::vector<int>& cellCost,
                                int width, int height, int sweeps, unsigned threads);
std::vector<int> responseMap(const std::vector<int>& initial, const std::vector<int>& cellCost,
                             int width, int height, int sweeps, KernelTiming* timing = nullptr);
int worstServedCell(const std::vector<int>& times, const std::vector<bool>& counted);
}
