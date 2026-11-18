#pragma once

#include <delivery/core/GeoBounds.hpp>

#include <cstdint>

namespace delivery::core {
struct PlanarPoint {
    std::int32_t xDm = 0;
    std::int32_t yDm = 0;
};

class PlanarProjection {
public:
    explicit PlanarProjection(LatLon origin);

    PlanarPoint toDecimeters(const LatLon& position) const;

private:
    LatLon origin_;
    double dmPerDegreeLon_;
    double dmPerDegreeLat_;
};
}
