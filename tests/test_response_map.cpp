#include <delivery/gpu/ResponseMap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <queue>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace delivery;

namespace {
std::vector<int> gridDijkstra(const std::vector<int>& initial, const std::vector<int>& cost,
                              int width, int height) {
    std::vector<int> best(initial);
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (int i = 0; i < width * height; ++i) {
        if (best[static_cast<std::size_t>(i)] < gpu::kResponseUnreachable) {
            queue.push({best[static_cast<std::size_t>(i)], i});
        }
    }
    while (!queue.empty()) {
        const auto [time, idx] = queue.top();
        queue.pop();
        if (time > best[static_cast<std::size_t>(idx)]) {
            continue;
        }
        const int x = idx % width;
        const int y = idx / width;
        const int neighbours[4][2] = {{x, y - 1}, {x, y + 1}, {x + 1, y}, {x - 1, y}};
        for (const auto& n : neighbours) {
            if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height) {
                continue;
            }
            const int next = n[1] * width + n[0];
            const int through = time + cost[static_cast<std::size_t>(next)];
            if (through < best[static_cast<std::size_t>(next)]) {
                best[static_cast<std::size_t>(next)] = through;
                queue.push({through, next});
            }
        }
    }
    return best;
}

std::vector<int> randomSeeds(std::mt19937_64& rng, int cells, double probability) {
    std::bernoulli_distribution seed(probability);
    std::vector<int> initial(static_cast<std::size_t>(cells), gpu::kResponseUnreachable);
    for (int& v : initial) {
        if (seed(rng)) {
            v = 0;
        }
    }
    return initial;
}

std::vector<int> randomCosts(std::mt19937_64& rng, int cells) {
    std::uniform_int_distribution<int> cost(10, 40);
    std::vector<int> costs(static_cast<std::size_t>(cells));
    for (int& c : costs) {
        c = cost(rng);
    }
    return costs;
}
}

TEST_CASE("Mapa de respuesta: un barrido avanza una celda desde cada repartidor libre") {
    const int side = 5;
    std::vector<int> initial(side * side, gpu::kResponseUnreachable);
    initial[2 * side + 2] = 0;
    const std::vector<int> cost(side * side, 10);
    const auto one = gpu::responseMapCpu(initial, cost, side, side, 1, 1);
    REQUIRE(one[2 * side + 2] == 0);
    REQUIRE(one[1 * side + 2] == 10);
    REQUIRE(one[2 * side + 1] == 10);
    REQUIRE(one[1 * side + 1] == gpu::kResponseUnreachable);
    const auto two = gpu::responseMapCpu(initial, cost, side, side, 2, 1);
    REQUIRE(two[1 * side + 1] == 20);
    REQUIRE(two[0] == gpu::kResponseUnreachable);
}

TEST_CASE("Mapa de respuesta: sin repartidores libres ninguna celda es alcanzable") {
    const int side = 4;
    const std::vector<int> initial(side * side, gpu::kResponseUnreachable);
    const std::vector<int> cost(side * side, 7);
    for (int t : gpu::responseMapCpu(initial, cost, side, side, 20, 1)) {
        REQUIRE(t == gpu::kResponseUnreachable);
    }
}

TEST_CASE("Mapa de respuesta: con barridos suficientes coincide con Dijkstra sobre la rejilla") {
    std::mt19937_64 rng(11);
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{6, 6}, {13, 7}, {20, 17}}) {
        const int cells = width * height;
        const auto initial = randomSeeds(rng, cells, 0.05);
        const auto cost = randomCosts(rng, cells);
        REQUIRE(gpu::responseMapCpu(initial, cost, width, height, cells, 1) ==
                gridDijkstra(initial, cost, width, height));
    }
}

TEST_CASE("Mapa de respuesta: CPU multihilo y kernel por bloques dan el mismo mapa") {
    std::mt19937_64 rng(12);
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{3, 3}, {16, 16}, {17, 9}, {40, 33}}) {
        const int cells = width * height;
        const auto initial = randomSeeds(rng, cells, 0.03);
        const auto cost = randomCosts(rng, cells);
        const auto reference = gpu::responseMapCpu(initial, cost, width, height, 25, 1);
        REQUIRE(gpu::responseMapCpu(initial, cost, width, height, 25, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::responseMap(initial, cost, width, height, 25, &timing) == reference);
    }
}

TEST_CASE("Histograma de respuesta: intervalos de 15 s, último intervalo abierto e inalcanzables aparte") {
    const std::vector<int> times = {0, 14, 15, 29, 30, 1'000'000, gpu::kResponseUnreachable, 7};
    const std::vector<unsigned char> counted = {1, 1, 1, 1, 1, 1, 1, 0};
    const auto bins = gpu::binResponseTimes(times, counted);
    REQUIRE(bins.size() == static_cast<std::size_t>(gpu::kResponseBins + 1));
    REQUIRE(bins[0] == 2);
    REQUIRE(bins[1] == 2);
    REQUIRE(bins[2] == 1);
    REQUIRE(bins[gpu::kResponseBins - 1] == 1);
    REQUIRE(bins[gpu::kResponseBins] == 1);
    unsigned total = 0;
    for (unsigned b : bins) {
        total += b;
    }
    REQUIRE(total == 7);
}

TEST_CASE("Histograma de respuesta: la versión encadenada en la GPU cuenta igual que la CPU") {
    std::mt19937_64 rng(13);
    std::bernoulli_distribution street(0.8);
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{3, 3}, {16, 16}, {17, 9}, {64, 64}, {100, 37}}) {
        const int cells = width * height;
        const auto initial = randomSeeds(rng, cells, 0.02);
        const auto cost = randomCosts(rng, cells);
        std::vector<unsigned char> counted(static_cast<std::size_t>(cells));
        unsigned expectedTotal = 0;
        for (auto& c : counted) {
            c = street(rng) ? 1 : 0;
            expectedTotal += c;
        }
        const auto reference = gpu::binResponseTimes(
            gpu::responseMapCpu(initial, cost, width, height, 30, 1), counted);
        REQUIRE(gpu::responseHistogramCpu(initial, cost, counted, width, height, 30, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::responseHistogram(initial, cost, counted, width, height, 30, &timing) == reference);
        unsigned total = 0;
        for (unsigned b : reference) {
            total += b;
        }
        REQUIRE(total == expectedTotal);
    }
}

TEST_CASE("Histograma de respuesta: con todas las celdas en el mismo intervalo no se pierde ningún conteo") {
    const int side = 200;
    const std::vector<int> initial(side * side, 0);
    const std::vector<int> cost(side * side, 10);
    const std::vector<unsigned char> counted(side * side, 1);
    const auto bins = gpu::responseHistogram(initial, cost, counted, side, side, 5);
    REQUIRE(bins[0] == static_cast<unsigned>(side * side));
}

TEST_CASE("Histograma de respuesta: la máscara debe tener una entrada por celda") {
    const std::vector<int> initial(9, 0);
    const std::vector<int> cost(9, 10);
    REQUIRE_THROWS_AS(gpu::responseHistogramCpu(initial, cost, std::vector<unsigned char>(8, 1), 3, 3, 1, 1),
                      std::invalid_argument);
}

TEST_CASE("Mapa de respuesta: la peor celda ignora las inalcanzables y desempata por índice") {
    const std::vector<int> times = {0, 30, gpu::kResponseUnreachable, 30, 50};
    REQUIRE(gpu::worstServedCell(times, {true, true, true, true, true}) == 4);
    REQUIRE(gpu::worstServedCell(times, {true, true, true, true, false}) == 1);
    REQUIRE(gpu::worstServedCell(times, {false, false, true, false, false}) == -1);
}
