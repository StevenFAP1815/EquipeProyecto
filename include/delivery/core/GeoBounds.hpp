#pragma once

#include <cstdint>
#include <utility>

namespace delivery::core {
struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

struct Pixel {
    int x = 0;
    int y = 0;
};

class GeoBounds {
public:
    GeoBounds() = default;
    GeoBounds(double north, double south, double west, double east);

    double north() const { return north_; }
    double south() const { return south_; }
    double west()  const { return west_;  }
    double east()  const { return east_;  }

    void validate() const;

    Pixel toPixel(const LatLon& geo, int width, int height) const;

    LatLon fromPixel(const Pixel& px, int width, int height) const;

    static double distanceMeters(const LatLon& a, const LatLon& b);

private:
    double north_ = 0.0;
    double south_ = 0.0;
    double west_  = 0.0;
    double east_  = 0.0;
};
}
