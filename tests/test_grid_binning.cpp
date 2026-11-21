#include <delivery/core/GridBinning.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numeric>

using delivery::core::binPoints;
using delivery::core::cellIndex;
using delivery::core::GeoBounds;
using delivery::core::LatLon;

TEST_CASE("Celdas: las esquinas del mapa caen en las celdas de las esquinas") {
    const GeoBounds bounds(10.0, 0.0, 0.0, 10.0);
    REQUIRE(cellIndex({9.99, 0.01}, bounds, 4) == 0);
    REQUIRE(cellIndex({9.99, 9.99}, bounds, 4) == 3);
    REQUIRE(cellIndex({0.01, 0.01}, bounds, 4) == 12);
    REQUIRE(cellIndex({0.01, 9.99}, bounds, 4) == 15);
}

TEST_CASE("Celdas: los puntos fuera del mapa se recortan al borde") {
    const GeoBounds bounds(10.0, 0.0, 0.0, 10.0);
    REQUIRE(cellIndex({20.0, -5.0}, bounds, 4) == 0);
    REQUIRE(cellIndex({-3.0, 15.0}, bounds, 4) == 15);
}

TEST_CASE("Celdas: el conteo conserva el número de puntos") {
    const GeoBounds bounds(10.0, 0.0, 0.0, 10.0);
    const std::vector<LatLon> points = {{1.0, 1.0}, {1.2, 1.1}, {8.0, 8.0}, {5.0, 5.0}};
    const auto counts = binPoints(points, bounds, 2);
    REQUIRE(counts.size() == 4);
    REQUIRE(counts[2] == 2);
    REQUIRE(counts[1] == 1);
    REQUIRE(counts[3] == 1);
    REQUIRE(std::accumulate(counts.begin(), counts.end(), 0) == 4);
}
