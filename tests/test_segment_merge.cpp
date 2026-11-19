#include <delivery/gpu/SegmentMerge.hpp>
#include <delivery/infra/EventLog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace delivery;

namespace {
void addSegment(gpu::Timeline& timeline, std::vector<int>& offsets, int source,
                const std::vector<long long>& times) {
    for (long long t : times) {
        timeline.t.push_back(t);
        timeline.source.push_back(source);
    }
    offsets.push_back(static_cast<int>(timeline.t.size()));
}
}

TEST_CASE("Mezcla de registros: en un empate de tiempo va primero el segmento anterior") {
    gpu::Timeline segments;
    std::vector<int> offsets{0};
    addSegment(segments, offsets, 0, {10, 20, 30});
    addSegment(segments, offsets, 1, {20, 25});
    addSegment(segments, offsets, 2, {});
    addSegment(segments, offsets, 3, {5, 30});

    const auto reference = gpu::mergeSegmentsReference(segments, offsets);
    REQUIRE(reference.t == std::vector<long long>{5, 10, 20, 20, 25, 30, 30});
    REQUIRE(reference.source == std::vector<int>{3, 0, 0, 1, 1, 0, 3});
    REQUIRE(gpu::mergeSegmentsCpu(segments, offsets, 1) == reference);
    REQUIRE(gpu::mergeSegmentsCpu(segments, offsets, 2) == reference);
    REQUIRE(gpu::mergeSegments(segments, offsets) == reference);
}

TEST_CASE("Mezcla de registros: CPU multihilo y GPU igualan a la ordenación estable") {
    std::mt19937_64 rng(77);
    std::uniform_int_distribution<long long> gap(0, 3);
    for (int segmentsCount : {1, 2, 5, 33}) {
        std::uniform_int_distribution<int> length(0, 120);
        gpu::Timeline segments;
        std::vector<int> offsets{0};
        for (int s = 0; s < segmentsCount; ++s) {
            std::vector<long long> times;
            long long t = 0;
            const int size = length(rng);
            for (int k = 0; k < size; ++k) {
                t += gap(rng);
                times.push_back(t);
            }
            addSegment(segments, offsets, s, times);
        }
        const auto reference = gpu::mergeSegmentsReference(segments, offsets);
        REQUIRE(std::is_sorted(reference.t.begin(), reference.t.end()));
        REQUIRE(gpu::mergeSegmentsCpu(segments, offsets, 1) == reference);
        REQUIRE(gpu::mergeSegmentsCpu(segments, offsets, 4) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::mergeSegments(segments, offsets, &timing) == reference);
    }
}

TEST_CASE("Mezcla de registros: sin entregas el resultado está vacío") {
    const gpu::Timeline empty;
    const std::vector<int> offsets{0};
    REQUIRE(gpu::mergeSegments(empty, offsets).t.empty());
    REQUIRE(gpu::mergeSegmentsCpu(empty, offsets, 3).t.empty());
}

TEST_CASE("EventLog: registra el instante y el repartidor de cada entrega") {
    infra::EventLog log;
    core::Order order;
    order.id = core::OrderId("o2");
    order.restaurant = core::RestaurantId("r1");
    log.orderCreated(0, order);
    log.orderDelivered(4200, order.id, core::CourierId("c9"));
    const auto stamps = log.deliveryStamps();
    REQUIRE(stamps.size() == 1);
    REQUIRE(stamps[0].t == 4200);
    REQUIRE(stamps[0].courier == "c9");
}
