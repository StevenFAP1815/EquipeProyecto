#include <delivery/gpu/GroupByKey.hpp>
#include <delivery/infra/EventLog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

using namespace delivery;

namespace {
gpu::Grouping stableSortReference(const std::vector<int>& keys, int keyCount) {
    gpu::Grouping g;
    g.order.resize(keys.size());
    std::iota(g.order.begin(), g.order.end(), 0);
    std::stable_sort(g.order.begin(), g.order.end(),
                     [&keys](int a, int b) { return keys[static_cast<std::size_t>(a)] <
                                                    keys[static_cast<std::size_t>(b)]; });
    g.offsets.assign(static_cast<std::size_t>(keyCount) + 1, 0);
    for (int k : keys) {
        ++g.offsets[static_cast<std::size_t>(k) + 1];
    }
    std::partial_sum(g.offsets.begin(), g.offsets.end(), g.offsets.begin());
    return g;
}
}

TEST_CASE("Agrupación por restaurante: igual a una ordenación estable por clave") {
    const std::vector<int> keys = {2, 0, 2, 1, 0, 2};
    const auto g = gpu::groupByKeyCpu(keys, 4, 1);
    REQUIRE(g.order == std::vector<int>{1, 4, 3, 0, 2, 5});
    REQUIRE(g.offsets == std::vector<int>{0, 2, 3, 6, 6});
    REQUIRE(g == stableSortReference(keys, 4));
}

TEST_CASE("Agrupación por restaurante: CPU multihilo y GPU con scan jerárquico coinciden") {
    std::mt19937_64 rng(41);
    for (int keyCount : {1, 7, 60}) {
        std::uniform_int_distribution<int> key(0, keyCount - 1);
        for (int n : {1, 255, 256, 257, 3000}) {
            std::vector<int> keys;
            for (int i = 0; i < n; ++i) {
                keys.push_back(key(rng));
            }
            const auto reference = stableSortReference(keys, keyCount);
            REQUIRE(gpu::groupByKeyCpu(keys, keyCount, 1) == reference);
            REQUIRE(gpu::groupByKeyCpu(keys, keyCount, 3) == reference);
            gpu::KernelTiming timing;
            REQUIRE(gpu::groupByKey(keys, keyCount, &timing) == reference);
        }
    }
}

TEST_CASE("Agrupación por restaurante: rechaza claves fuera de rango") {
    REQUIRE_THROWS(gpu::groupByKeyCpu({0, 5}, 3, 1));
    REQUIRE_THROWS(gpu::groupByKey({-1}, 3));
}

TEST_CASE("EventLog: recuerda el restaurante de cada entrega") {
    infra::EventLog log;
    core::Order order;
    order.id = core::OrderId("o7");
    order.restaurant = core::RestaurantId("r3");
    order.createdAtMs = 0;
    log.orderCreated(0, order);
    log.orderDelivered(1000, order.id, core::CourierId("c1"));
    REQUIRE(log.deliveryRestaurants() == std::vector<std::string>{"r3"});
}
