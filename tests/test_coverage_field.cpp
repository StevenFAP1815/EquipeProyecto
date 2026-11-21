#include <delivery/gpu/CoverageField.hpp>

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

using namespace delivery;

TEST_CASE("Campo de cobertura: un barrido de Jacobi promedia los vecinos y suma la fuente") {
    const int side = 5;
    std::vector<float> density(side * side, 0.0f);
    density[2 * side + 2] = 4.0f;
    const auto one = gpu::coverageFieldCpu(density, side, side, 1, 1);
    REQUIRE(one[2 * side + 2] == 1.0f);
    REQUIRE(one[2 * side + 1] == 0.0f);
    const auto two = gpu::coverageFieldCpu(density, side, side, 2, 1);
    REQUIRE(two[2 * side + 1] == 0.25f);
    REQUIRE(two[2 * side + 2] == 1.0f);
}

TEST_CASE("Campo de cobertura: los bordes quedan fijos en cero") {
    const int side = 6;
    const std::vector<float> density(side * side, 1.0f);
    const auto field = gpu::coverageFieldCpu(density, side, side, 30, 1);
    for (int i = 0; i < side; ++i) {
        REQUIRE(field[i] == 0.0f);
        REQUIRE(field[(side - 1) * side + i] == 0.0f);
        REQUIRE(field[i * side] == 0.0f);
        REQUIRE(field[i * side + side - 1] == 0.0f);
    }
    REQUIRE(field[2 * side + 2] > 0.0f);
}

TEST_CASE("Campo de cobertura: CPU multihilo y kernel por bloques dan el mismo campo") {
    std::mt19937_64 rng(8);
    std::poisson_distribution<int> couriers(0.7);
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{3, 3}, {16, 16}, {17, 9}, {40, 33}}) {
        std::vector<float> density(static_cast<std::size_t>(width) * height);
        for (float& d : density) {
            d = static_cast<float>(couriers(rng));
        }
        const auto reference = gpu::coverageFieldCpu(density, width, height, 25, 1);
        REQUIRE(gpu::coverageFieldCpu(density, width, height, 25, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::coverageField(density, width, height, 25, &timing) == reference);
    }
}

TEST_CASE("Campo de cobertura: la celda menos cubierta es interior y desempata por índice") {
    const int side = 4;
    std::vector<float> field(side * side, 0.0f);
    field[1 * side + 1] = 2.0f;
    field[1 * side + 2] = 1.0f;
    field[2 * side + 1] = 1.0f;
    field[2 * side + 2] = 3.0f;
    REQUIRE(gpu::leastCoveredInteriorCell(field, side, side) == 1 * side + 2);
    REQUIRE(gpu::leastCoveredInteriorCell(field, 2, 2) == -1);
}
