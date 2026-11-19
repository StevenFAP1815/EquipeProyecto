#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kHeatRadius = 2;
constexpr int kHeatWidth = 2 * kHeatRadius + 1;
constexpr int kHeatTile = 16;
constexpr int kHeatWeightSum = 256;

std::vector<int> heatFilterWeights();
std::vector<int> smoothDemandCpu(const std::vector<int>& counts, int width, int height,
                                 unsigned threads);
std::vector<int> smoothDemand(const std::vector<int>& counts, int width, int height,
                              KernelTiming* timing = nullptr);
}
