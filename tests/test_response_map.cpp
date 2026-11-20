#include <delivery/gpu/ResponseMap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <queue>
#include <random>
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

TEST_CASE("Mapa de respuesta: la peor celda ignora las inalcanzables y desempata por índice") {
    const std::vector<int> times = {0, 30, gpu::kResponseUnreachable, 30, 50};
    REQUIRE(gpu::worstServedCell(times, {true, true, true, true, true}) == 4);
    REQUIRE(gpu::worstServedCell(times, {true, true, true, true, false}) == 1);
    REQUIRE(gpu::worstServedCell(times, {false, false, true, false, false}) == -1);
}
