#include <delivery/core/GeoBounds.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace delivery::core {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371000.0;

double toRadians(double deg) {
    return deg * kPi / 180.0;
}
}  // namespace

GeoBounds::GeoBounds(double north, double south, double west, double east)
    : north_(north), south_(south), west_(west), east_(east) {}

void GeoBounds::validate() const {
    if (north_ <= south_) {
        throw std::invalid_argument(
            "GeoBounds: north (" + std::to_string(north_) +
            ") must be greater than south (" + std::to_string(south_) + ")");
    }
    if (east_ <= west_) {
        throw std::invalid_argument(
            "GeoBounds: east (" + std::to_string(east_) +
            ") must be greater than west (" + std::to_string(west_) + ")");
    }
}

Pixel GeoBounds::toPixel(const LatLon& geo, int width, int height) const {
    // Normalizamos a [0, 1].
    const double u = (geo.lon - west_) / (east_ - west_);
    // La latitud crece hacia el norte, el pixel Y crece hacia abajo.
    const double v = (north_ - geo.lat) / (north_ - south_);

    Pixel px;
    px.x = static_cast<int>(std::lround(u * (width - 1)));
    px.y = static_cast<int>(std::lround(v * (height - 1)));
    return px;
}

LatLon GeoBounds::fromPixel(const Pixel& px, int width, int height) const {
    const double u = static_cast<double>(px.x) / static_cast<double>(width - 1);
    const double v = static_cast<double>(px.y) / static_cast<double>(height - 1);

    LatLon geo;
    geo.lon = west_ + u * (east_ - west_);
    geo.lat = north_ - v * (north_ - south_);
    return geo;
}

double GeoBounds::distanceMeters(const LatLon& a, const LatLon& b) {
    const double lat1 = toRadians(a.lat);
    const double lat2 = toRadians(b.lat);
    const double dLat = toRadians(b.lat - a.lat);
    const double dLon = toRadians(b.lon - a.lon);

    const double sinDLat = std::sin(dLat / 2.0);
    const double sinDLon = std::sin(dLon / 2.0);

    const double h = sinDLat * sinDLat +
                     std::cos(lat1) * std::cos(lat2) * sinDLon * sinDLon;
    const double c = 2.0 * std::asin(std::min(1.0, std::sqrt(h)));

    return kEarthRadiusM * c;
}

}  // namespace delivery::core