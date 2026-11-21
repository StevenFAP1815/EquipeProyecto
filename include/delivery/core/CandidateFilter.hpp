#pragma once

#include <cstdint>
#include <vector>

namespace delivery::core {
std::vector<int> filterWithinRadiusCpu(const std::vector<std::int32_t>& xDm,
                                       const std::vector<std::int32_t>& yDm,
                                       const std::vector<std::uint8_t>& available,
                                       std::int32_t centerXDm, std::int32_t centerYDm,
                                       std::int32_t radiusDm, unsigned threads);
}
