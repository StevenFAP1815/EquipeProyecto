#include <delivery/gpu/DeliveryHistogram.hpp>
#include <delivery/infra/EventLog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <random>
#include <vector>

using namespace delivery;

TEST_CASE("Histograma de entregas: un intervalo por minuto y el último acumula el resto") {
    const std::vector<long long> durations = {0, 59999, 60000, 125000, -5, 999999999};
    const auto bins = gpu::deliveryHistogramCpu(durations, 1);
    REQUIRE(bins.size() == static_cast<std::size_t>(gpu::kHistogramBins));
    REQUIRE(bins[0] == 3);
    REQUIRE(bins[1] == 1);
    REQUIRE(bins[2] == 1);
    REQUIRE(bins[gpu::kHistogramBins - 1] == 1);
}

TEST_CASE("Histograma de entregas: CPU multihilo y kernel privatizado cuentan lo mismo") {
    std::mt19937_64 rng(13);
    std::uniform_int_distribution<long long> ms(0, 200LL * 60000);
    for (int n : {0, 1, 255, 1000, 20000}) {
        std::vector<long long> durations;
        for (int i = 0; i < n; ++i) {
            durations.push_back(ms(rng));
        }
        const auto reference = gpu::deliveryHistogramCpu(durations, 1);
        REQUIRE(std::accumulate(reference.begin(), reference.end(), 0ull) ==
                static_cast<unsigned long long>(n));
        REQUIRE(gpu::deliveryHistogramCpu(durations, 4) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::deliveryHistogram(durations, &timing) == reference);
    }
}

TEST_CASE("Histograma de entregas: la descripción lista solo los intervalos con entregas") {
    std::vector<unsigned> bins(gpu::kHistogramBins, 0);
    bins[3] = 2;
    bins[gpu::kHistogramBins - 1] = 1;
    const std::string text = gpu::describeHistogram(bins);
    REQUIRE(text.find("3 deliveries") != std::string::npos);
    REQUIRE(text.find(" 3m=2") != std::string::npos);
    REQUIRE(text.find("127+m=1") != std::string::npos);
}

TEST_CASE("EventLog: guarda la duración de cada entrega para las estadísticas") {
    infra::EventLog log;
    core::Order order;
    order.id = core::OrderId("o1");
    order.restaurant = core::RestaurantId("r0");
    order.createdAtMs = 1000;
    log.orderCreated(1000, order);
    log.orderDelivered(61000, order.id, core::CourierId("c0"));
    REQUIRE(log.deliveryDurationsMs() == std::vector<std::int64_t>{60000});
}
