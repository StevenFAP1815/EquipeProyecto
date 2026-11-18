#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <cstdint>
#include <vector>

namespace delivery::gpu {
constexpr int kFwTile = 32;
constexpr std::int32_t kFwInfinity = 0x3fffffff;

std::vector<std::int32_t> floydWarshallTiled(int n, const std::vector<std::int32_t>& adjacency,
                                             KernelTiming* timing = nullptr);
}
