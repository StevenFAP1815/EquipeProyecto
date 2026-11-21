#include <delivery/core/GridBinning.hpp>

#include <algorithm>

namespace delivery::core {
int cellIndex(const LatLon& point, const GeoBounds& bounds, int cells) {
    const double fx = (point.lon - bounds.west()) / (bounds.east() - bounds.west());
    const double fy = (bounds.north() - point.lat) / (bounds.north() - bounds.south());
    const int cx = std::clamp(static_cast<int>(fx * cells), 0, cells - 1);
    const int cy = std::clamp(static_cast<int>(fy * cells), 0, cells - 1);
    return cy * cells + cx;
}

std::vector<int> binPoints(const std::vector<LatLon>& points, const GeoBounds& bounds, int cells) {
    std::vector<int> counts(static_cast<std::size_t>(cells) * cells, 0);
    for (const LatLon& point : points) {
        ++counts[static_cast<std::size_t>(cellIndex(point, bounds, cells))];
    }
    return counts;
}
}
