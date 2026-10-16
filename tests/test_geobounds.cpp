#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdlib>

#include <delivery/core/GeoBounds.hpp>

using namespace delivery::core;
using Catch::Matchers::WithinAbs;

namespace {

// Caja de prueba: 0.02 grados de lado, ~2.2 km en cada direccion a la latitud
// de Santa Cruz de la Sierra (~-17.76). Valores ficticios para tests.
GeoBounds makeTestBounds() {
    return GeoBounds(
        /*north=*/ -17.7500,
        /*south=*/ -17.7700,
        /*west =*/ -63.2000,
        /*east =*/ -63.1800
    );
}

constexpr int kWidth  = 800;
constexpr int kHeight = 600;

}  // namespace

TEST_CASE("GeoBounds: validate acepta cajas validas", "[geobounds]") {
    const GeoBounds b = makeTestBounds();
    REQUIRE_NOTHROW(b.validate());
}

TEST_CASE("GeoBounds: validate rechaza north <= south", "[geobounds]") {
    const GeoBounds b(
        /*north=*/ -17.7700,
        /*south=*/ -17.7500,
        /*west =*/ -63.2000,
        /*east =*/ -63.1800
    );
    REQUIRE_THROWS_AS(b.validate(), std::invalid_argument);
}

TEST_CASE("GeoBounds: validate rechaza east <= west", "[geobounds]") {
    const GeoBounds b(
        /*north=*/ -17.7500,
        /*south=*/ -17.7700,
        /*west =*/ -63.1800,
        /*east =*/ -63.2000
    );
    REQUIRE_THROWS_AS(b.validate(), std::invalid_argument);
}

TEST_CASE("GeoBounds: esquina NW mapea a pixel (0,0)", "[geobounds]") {
    const GeoBounds b = makeTestBounds();
    const LatLon nw{b.north(), b.west()};
    const Pixel p = b.toPixel(nw, kWidth, kHeight);
    REQUIRE(p.x == 0);
    REQUIRE(p.y == 0);
}

TEST_CASE("GeoBounds: esquina SE mapea a pixel (width-1, height-1)",
          "[geobounds]") {
    const GeoBounds b = makeTestBounds();
    const LatLon se{b.south(), b.east()};
    const Pixel p = b.toPixel(se, kWidth, kHeight);
    REQUIRE(p.x == kWidth - 1);
    REQUIRE(p.y == kHeight - 1);
}

TEST_CASE("GeoBounds: centro mapea cerca del centro", "[geobounds]") {
    const GeoBounds b = makeTestBounds();
    const LatLon center{(b.north() + b.south()) / 2.0,
                        (b.west() + b.east()) / 2.0};
    const Pixel p = b.toPixel(center, kWidth, kHeight);
    // Permitimos +-1 pixel por el redondeo.
    REQUIRE(std::abs(p.x - kWidth / 2) <= 1);
    REQUIRE(std::abs(p.y - kHeight / 2) <= 1);
}

TEST_CASE("GeoBounds: latitud crece hacia arriba en Y", "[geobounds]") {
    // Norte debe dar Y menor que sur (Y crece hacia abajo en la imagen).
    const GeoBounds b = makeTestBounds();
    const double midLon = (b.west() + b.east()) / 2.0;
    const Pixel pNorth = b.toPixel({b.north(), midLon}, kWidth, kHeight);
    const Pixel pSouth = b.toPixel({b.south(), midLon}, kWidth, kHeight);
    REQUIRE(pNorth.y < pSouth.y);
}

TEST_CASE("GeoBounds: round-trip pixel -> geo -> pixel es identidad",
          "[geobounds]") {
    const GeoBounds b = makeTestBounds();
    const Pixel original{123, 456};
    const LatLon geo = b.fromPixel(original, kWidth, kHeight);
    const Pixel back = b.toPixel(geo, kWidth, kHeight);
    REQUIRE(back.x == original.x);
    REQUIRE(back.y == original.y);
}

TEST_CASE("GeoBounds: distanceMeters es 0 para el mismo punto",
          "[geobounds]") {
    const LatLon p{-17.7600, -63.1900};
    REQUIRE_THAT(GeoBounds::distanceMeters(p, p), WithinAbs(0.0, 1e-6));
}

TEST_CASE("GeoBounds: distanceMeters es simetrica", "[geobounds]") {
    const LatLon a{-17.7500, -63.2000};
    const LatLon b{-17.7700, -63.1800};
    const double d1 = GeoBounds::distanceMeters(a, b);
    const double d2 = GeoBounds::distanceMeters(b, a);
    REQUIRE_THAT(d1, WithinAbs(d2, 1e-6));
}

TEST_CASE("GeoBounds: distanceMeters de 0.02 grados ~ 2.2 km", "[geobounds]") {
    // 0.02 grados de latitud ~ 2.22 km. En una esfera.
    const LatLon a{-17.7500, -63.1900};
    const LatLon b{-17.7700, -63.1900};
    const double d = GeoBounds::distanceMeters(a, b);
    // Tolerancia amplia: 2.0 - 2.5 km.
    REQUIRE(d > 2000.0);
    REQUIRE(d < 2500.0);
}