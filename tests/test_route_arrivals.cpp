#include <delivery/gpu/RouteArrivals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

using namespace delivery;

namespace {
struct Routes {
    std::vector<int> leg;
    std::vector<unsigned char> start;
    std::vector<int> deadline;
};

Routes randomRoutes(std::mt19937_64& rng, int legs, int maxStops) {
    std::uniform_int_distribution<int> stops(1, maxStops);
    std::uniform_int_distribution<int> legMs(30'000, 600'000);
    std::uniform_int_distribution<int> deadline(0, 2'400'000);
    std::bernoulli_distribution pickupFirst(0.5);
    Routes r;
    while (static_cast<int>(r.leg.size()) < legs) {
        const int count = std::min(stops(rng), legs - static_cast<int>(r.leg.size()));
        const bool pickup = pickupFirst(rng);
        for (int k = 0; k < count; ++k) {
            r.leg.push_back(legMs(rng));
            r.start.push_back(k == 0 ? 1 : 0);
            r.deadline.push_back(k == 0 && pickup ? gpu::kNoDeadline : deadline(rng));
        }
    }
    return r;
}
}

TEST_CASE("Llegadas por ruta: la suma acumulada vuelve a cero al empezar cada ruta") {
    const std::vector<int> leg = {5, 10, 20, 7, 3};
    const std::vector<unsigned char> start = {1, 0, 0, 1, 0};
    const std::vector<int> deadline = {gpu::kNoDeadline, 14, 40, gpu::kNoDeadline, 9};
    const auto result = gpu::routeArrivalsCpu(leg, start, deadline, 1);
    REQUIRE(result.arrivalMs == std::vector<int>{5, 15, 35, 7, 10});
    REQUIRE(result.late == 2);
}

TEST_CASE("Llegadas por ruta: llegar justo a la hora prometida no es tarde") {
    const auto result = gpu::routeArrivalsCpu({10, 10}, {1, 0}, {10, 20}, 1);
    REQUIRE(result.late == 0);
}

TEST_CASE("Llegadas por ruta: CPU multihilo y GPU coinciden con la CPU de un hilo") {
    std::mt19937_64 rng(21);
    for (int legs : {1, 7, 511, 512, 513, 5'000, 40'000}) {
        const Routes r = randomRoutes(rng, legs, 4);
        const auto reference = gpu::routeArrivalsCpu(r.leg, r.start, r.deadline, 1);
        REQUIRE(gpu::routeArrivalsCpu(r.leg, r.start, r.deadline, 3) == reference);
        gpu::KernelTiming timing;
        REQUIRE(gpu::routeArrivals(r.leg, r.start, r.deadline, &timing) == reference);
    }
}

TEST_CASE("Llegadas por ruta: una ruta que cruza varios bloques recibe el arrastre de todos") {
    const int legs = 3 * gpu::kRouteScanBlock + 17;
    std::vector<int> leg(legs, 1);
    std::vector<unsigned char> start(legs, 0);
    start[0] = 1;
    start[legs - 5] = 1;
    const std::vector<int> deadline(legs, gpu::kNoDeadline);
    const auto result = gpu::routeArrivals(leg, start, deadline);
    REQUIRE(result.arrivalMs[legs - 6] == legs - 5);
    REQUIRE(result.arrivalMs[legs - 5] == 1);
    REQUIRE(result.arrivalMs[legs - 1] == 5);
}

TEST_CASE("Llegadas por ruta: más bloques que hilos del kernel de arrastres") {
    std::mt19937_64 rng(22);
    const Routes r = randomRoutes(rng, (gpu::kRouteCarryBlock + 20) * gpu::kRouteScanBlock, 9);
    REQUIRE(gpu::routeArrivals(r.leg, r.start, r.deadline) ==
            gpu::routeArrivalsCpu(r.leg, r.start, r.deadline, 1));
}

TEST_CASE("Llegadas por ruta: la primera etapa debe empezar una ruta y los tamaños coincidir") {
    REQUIRE_THROWS_AS(gpu::routeArrivalsCpu({1, 2}, {0, 1}, {gpu::kNoDeadline, gpu::kNoDeadline}, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gpu::routeArrivalsCpu({1, 2}, {1, 0}, {gpu::kNoDeadline}, 1),
                      std::invalid_argument);
    REQUIRE(gpu::routeArrivals({}, {}, {}).arrivalMs.empty());
}
