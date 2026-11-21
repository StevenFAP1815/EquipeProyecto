#include <delivery/gpu/RadixSort.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace delivery;

TEST_CASE("Percentiles: rango más cercano sobre los tiempos ordenados") {
    std::vector<unsigned> sorted(100);
    for (unsigned i = 0; i < 100; ++i) {
        sorted[i] = i + 1;
    }
    const auto p = gpu::percentilesOfSorted(sorted);
    REQUIRE(p.p50 == 50);
    REQUIRE(p.p90 == 90);
    REQUIRE(p.p99 == 99);
    REQUIRE(gpu::percentilesOfSorted({7}) == gpu::Percentiles{7, 7, 7});
    REQUIRE(gpu::percentilesOfSorted({}) == gpu::Percentiles{});
}

TEST_CASE("Ordenación: las duraciones se convierten en claves sin signo acotadas") {
    REQUIRE(gpu::toSortKeys({-3, 0, 42, 5000000000LL}) ==
            std::vector<unsigned>{0, 0, 42, 0xffffffffu});
}

TEST_CASE("Ordenación: CPU multihilo y radix sort de la GPU igualan a std::sort") {
    std::mt19937_64 rng(31);
    std::uniform_int_distribution<unsigned> key(0, 4095);
    for (int n : {1, 2, 255, 256, 257, 600}) {
        std::vector<unsigned> keys;
        for (int i = 0; i < n; ++i) {
            keys.push_back(key(rng));
        }
        std::vector<unsigned> expected = keys;
        std::sort(expected.begin(), expected.end());
        REQUIRE(gpu::sortKeysCpu(keys, 1) == expected);
        REQUIRE(gpu::sortKeysCpu(keys, 3) == expected);
        gpu::KernelTiming timing;
        REQUIRE(gpu::radixSort(keys, &timing) == expected);
        REQUIRE(gpu::percentilesOfSorted(gpu::radixSort(keys)) ==
                gpu::percentilesOfSorted(expected));
    }
}

TEST_CASE("Ordenación: un arreglo vacío queda vacío") {
    REQUIRE(gpu::radixSort({}).empty());
    REQUIRE(gpu::sortKeysCpu({}, 4).empty());
}
