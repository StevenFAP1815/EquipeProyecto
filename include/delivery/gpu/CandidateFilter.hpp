#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <cstdint>
#include <vector>

namespace delivery::gpu {
constexpr int kFilterBlock = 256;

std::vector<int> filterWithinRadius(const std::vector<std::int32_t>& xDm,
                                    const std::vector<std::int32_t>& yDm,
                                    const std::vector<std::uint8_t>& available,
                                    std::int32_t centerXDm, std::int32_t centerYDm,
                                    std::int32_t radiusDm, KernelTiming* timing = nullptr);
}
