#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kStencilTile = 16;

std::vector<float> coverageFieldCpu(const std::vector<float>& density, int width, int height,
                                    int sweeps, unsigned threads);
std::vector<float> coverageField(const std::vector<float>& density, int width, int height,
                                 int sweeps, KernelTiming* timing = nullptr);
int leastCoveredInteriorCell(const std::vector<float>& field, int width, int height);
}
