#include <delivery/core/PlanarProjection.hpp>

#include <cmath>

namespace delivery::core {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusDm = 63710000.0;
}

PlanarProjection::PlanarProjection(LatLon origin)
    : origin_(origin),
      dmPerDegreeLon_(kEarthRadiusDm * kPi / 180.0 * std::cos(origin.lat * kPi / 180.0)),
      dmPerDegreeLat_(kEarthRadiusDm * kPi / 180.0) {}

PlanarPoint PlanarProjection::toDecimeters(const LatLon& position) const {
    return PlanarPoint{
        static_cast<std::int32_t>(std::lround((position.lon - origin_.lon) * dmPerDegreeLon_)),
        static_cast<std::int32_t>(std::lround((position.lat - origin_.lat) * dmPerDegreeLat_))};
}
}
