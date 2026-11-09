#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/OrderGenerator.hpp>
#include <delivery/sim/SimClock.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace delivery;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;

namespace {
infra::Config makeGeneratorConfig(std::uint64_t seed) {
    infra::Config cfg;
    cfg.nodes = {
        core::Node{core::NodeId("n0"), -17.7600, -63.1950},
        core::Node{core::NodeId("n1"), -17.7600, -63.1900},
        core::Node{core::NodeId("n2"), -17.7650, -63.1900},
    };
    cfg.restaurants = {
        core::Restaurant{core::RestaurantId("r0"), "R0", core::NodeId("n0"), 1, {1000, 2000}},
        core::Restaurant{core::RestaurantId("r1"), "R1", core::NodeId("n1"), 1, {1000, 2000}},
    };
    cfg.orders.meanIntervalMs = 1000;
    cfg.orders.burstMax = 5;
    cfg.orders.maxPending = 100000;
    cfg.orders.seed = seed;
    cfg.simulation.durationS = 100;
    cfg.simulation.timeScale = 1'000'000.0;
    return cfg;
}

std::vector<std::string> generateOrders(std::uint64_t seed) {
    const infra::Config cfg = makeGeneratorConfig(seed);
    concurrency::OrderBook book(100000);
    concurrency::ShutdownToken stop;
    sim::SimClock clock(cfg.simulation.timeScale);
    std::atomic<std::uint64_t> counter{0};

    sim::OrderGenerator generator(cfg, book, stop, clock, counter);
    generator.run();

    std::vector<std::string> orders;
    while (book.size() > 0) {
        const auto o = book.waitAndPop(stop);
        orders.push_back(o->id.str() + "|" + o->restaurant.str() + "|" +
                         o->deliveryNode.str() + "|" +
                         std::to_string(o->createdAtMs));
    }
    return orders;
}

void waitUntilIdle(const sim::Courier& courier) {
    while (courier.state() != sim::CourierState::Idle) {
        std::this_thread::sleep_for(1ms);
    }
}
}

TEST_CASE("OrderGenerator: la misma semilla produce exactamente los mismos pedidos",
          "[generator][reproducible]") {
    const auto first = generateOrders(42);
    const auto second = generateOrders(42);

    REQUIRE(first.size() > 50);
    REQUIRE(first == second);
}

TEST_CASE("OrderGenerator: otra semilla produce otros pedidos",
          "[generator][reproducible]") {
    REQUIRE(generateOrders(42) != generateOrders(7));
}

TEST_CASE("Courier: el segundo viaje parte desde donde termino el primero",
          "[courier]") {
    infra::Config cfg;
    cfg.nodes = {
        core::Node{core::NodeId("n0"), -17.7600, -63.2000},
        core::Node{core::NodeId("n1"), -17.7600, -63.1950},
        core::Node{core::NodeId("n2"), -17.7600, -63.1900},
        core::Node{core::NodeId("n3"), -17.7600, -63.1850},
    };
    cfg.streets = {
        core::Street{core::StreetId("s0"), core::NodeId("n0"), core::NodeId("n1"), true},
        core::Street{core::StreetId("s1"), core::NodeId("n1"), core::NodeId("n2"), true},
        core::Street{core::StreetId("s2"), core::NodeId("n2"), core::NodeId("n3"), true},
    };
    auto network = core::StreetNetwork::build(cfg);

    concurrency::ShutdownToken stop;
    sim::SimClock clock(2000.0);
    sim::Courier courier(core::CourierId("c0"), network, clock, stop, 30.0,
                         core::NodeId("n0"));
    std::thread worker([&courier] { courier.run(); });

    REQUIRE(courier.setTarget(core::NodeId("n2")));
    waitUntilIdle(courier);
    REQUIRE(courier.currentNode() == core::NodeId("n2"));

    const std::int64_t secondTripStart = clock.nowMs();
    REQUIRE(courier.setTarget(core::NodeId("n3")));
    waitUntilIdle(courier);
    const std::int64_t secondTripMs = courier.lastIdleMs() - secondTripStart;

    const double segmentMeters = *network->edgeLength(core::NodeId("n2"), core::NodeId("n3"));
    const double segmentMs = segmentMeters / (30.0 / 3600.0);

    REQUIRE(courier.currentNode() == core::NodeId("n3"));
    REQUIRE(secondTripMs < 2.0 * segmentMs);

    const auto end = courier.position();
    REQUIRE_THAT(end.lat, WithinAbs(cfg.nodes[3].lat, 1e-9));
    REQUIRE_THAT(end.lon, WithinAbs(cfg.nodes[3].lon, 1e-9));

    stop.requestStop();
    clock.wakeAll();
    courier.wake();
    worker.join();
}
