#include <delivery/core/CandidateFilter.hpp>
#include <delivery/core/PlanarProjection.hpp>
#include <delivery/gpu/CandidateFilter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

using namespace delivery;

namespace {
struct Fleet {
    std::vector<std::int32_t> x;
    std::vector<std::int32_t> y;
    std::vector<std::uint8_t> available;
};

Fleet randomFleet(int n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int32_t> coordinate(-30000, 30000);
    std::bernoulli_distribution free(0.6);
    Fleet f;
    for (int i = 0; i < n; ++i) {
        f.x.push_back(coordinate(rng));
        f.y.push_back(coordinate(rng));
        f.available.push_back(free(rng) ? 1 : 0);
    }
    return f;
}
}

TEST_CASE("Filtro de candidatos: incluye el borde, excluye lo ocupado y conserva el orden") {
    const std::vector<std::int32_t> x = {0, 300, 400, -400, 0, 10};
    const std::vector<std::int32_t> y = {0, 400, 300, 0, 501, 0};
    const std::vector<std::uint8_t> available = {1, 1, 0, 1, 1, 1};
    const auto inside = core::filterWithinRadiusCpu(x, y, available, 0, 0, 500, 1);
    REQUIRE(inside == std::vector<int>{0, 1, 3, 5});
}

TEST_CASE("Filtro de candidatos: la versión multihilo conserva el mismo orden") {
    const Fleet f = randomFleet(5000, 3);
    const auto one = core::filterWithinRadiusCpu(f.x, f.y, f.available, 1000, -2000, 15000, 1);
    for (unsigned threads : {2u, 3u, 8u}) {
        REQUIRE(core::filterWithinRadiusCpu(f.x, f.y, f.available, 1000, -2000, 15000, threads) ==
                one);
    }
    REQUIRE_FALSE(one.empty());
}

TEST_CASE("Filtro de candidatos: el kernel compacta igual que la CPU, en el mismo orden") {
    for (int n : {1, 255, 256, 257, 1000}) {
        const Fleet f = randomFleet(n, 17 + static_cast<std::uint64_t>(n));
        const auto expected = core::filterWithinRadiusCpu(f.x, f.y, f.available, 0, 0, 20000, 1);
        gpu::KernelTiming timing;
        REQUIRE(gpu::filterWithinRadius(f.x, f.y, f.available, 0, 0, 20000, &timing) == expected);
    }
}

TEST_CASE("Filtro de candidatos: sin nadie disponible el resultado está vacío") {
    const std::vector<std::int32_t> x = {0, 1};
    const std::vector<std::int32_t> y = {0, 1};
    const std::vector<std::uint8_t> available = {0, 0};
    REQUIRE(core::filterWithinRadiusCpu(x, y, available, 0, 0, 100, 1).empty());
    REQUIRE(gpu::filterWithinRadius(x, y, available, 0, 0, 100).empty());
}

TEST_CASE("Proyección plana: las distancias en decímetros se parecen a las geodésicas") {
    const core::LatLon origin{-17.78, -63.18};
    const core::PlanarProjection projection(origin);
    const core::LatLon east{-17.78, -63.17};
    const core::LatLon north{-17.77, -63.18};
    const auto e = projection.toDecimeters(east);
    const auto n = projection.toDecimeters(north);
    REQUIRE(std::abs(e.xDm / 10.0 - core::GeoBounds::distanceMeters(origin, east)) < 5.0);
    REQUIRE(std::abs(n.yDm / 10.0 - core::GeoBounds::distanceMeters(origin, north)) < 5.0);
    REQUIRE(projection.toDecimeters(origin).xDm == 0);
}
