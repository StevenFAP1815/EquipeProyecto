#pragma once

#include <delivery/core/GeoBounds.hpp>

#include <vector>

namespace delivery::core {
int cellIndex(const LatLon& point, const GeoBounds& bounds, int cells);
std::vector<int> binPoints(const std::vector<LatLon>& points, const GeoBounds& bounds, int cells);
}
