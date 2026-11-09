#include <catch2/catch_test_macros.hpp>

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace delivery;
using namespace std::chrono_literals;

namespace {
infra::Config makeLineConfig(int couriers) {
    infra::Config cfg;
    cfg.nodes = {
        core::Node{core::NodeId("n0"), -17.7600, -63.1950},
        core::Node{core::NodeId("n1"), -17.7600, -63.1900},
        core::Node{core::NodeId("n2"), -17.7600, -63.1850},
    };
    cfg.streets = {
        core::Street{core::StreetId("s0"), core::NodeId("n0"), core::NodeId("n1"), false},
        core::Street{core::StreetId("s1"), core::NodeId("n1"), core::NodeId("n2"), false},
    };
    cfg.fleet.couriers = couriers;
    cfg.fleet.speedKmh = 1.0;
    cfg.fleet.startNode = core::NodeId("n0");
    return cfg;
}
}

TEST_CASE("SimClock: waitUntilMs espera hasta el tiempo simulado pedido",
          "[simclock]") {
    concurrency::ShutdownToken stop;
    sim::SimClock clock(100.0);

    REQUIRE(clock.waitUntilMs(2000, stop));
    REQUIRE(clock.nowMs() >= 2000);
}

TEST_CASE("SimClock: un hilo esperando despierta al pedir el apagado",
          "[simclock][shutdown][concurrent]") {
    concurrency::ShutdownToken stop;
    sim::SimClock clock(1.0);

    auto waiter = std::async(std::launch::async, [&] {
        return clock.waitUntilMs(1'000'000'000, stop);
    });

    std::this_thread::sleep_for(20ms);
    stop.requestStop();
    clock.wakeAll();

    REQUIRE(waiter.wait_for(2s) == std::future_status::ready);
    REQUIRE_FALSE(waiter.get());
}

TEST_CASE("OrderBook: el apagado nunca se pierde aunque llegue al mismo tiempo que la espera",
          "[orderbook][shutdown][concurrent]") {
    for (int round = 0; round < 300; ++round) {
        concurrency::OrderBook book(10);
        concurrency::ShutdownToken stop;

        auto consumer = std::async(std::launch::async, [&] {
            return book.waitAndPop(stop).has_value();
        });

        stop.requestStop();
        book.wakeAll();

        REQUIRE(consumer.wait_for(2s) == std::future_status::ready);
        REQUIRE_FALSE(consumer.get());
    }
}

TEST_CASE("Fleet: los repartidores inactivos terminan al pedir el apagado",
          "[fleet][shutdown][concurrent]") {
    const infra::Config cfg = makeLineConfig(4);
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    sim::SimClock clock(1.0);
    concurrency::ChangeSignal freed;
    sim::Fleet fleet(cfg, network, clock, stop, freed);
    fleet.start();

    std::this_thread::sleep_for(20ms);
    stop.requestStop();
    clock.wakeAll();

    auto joined = std::async(std::launch::async, [&] { fleet.join(); });
    REQUIRE(joined.wait_for(2s) == std::future_status::ready);
}

TEST_CASE("Fleet: un repartidor en movimiento termina al pedir el apagado",
          "[fleet][shutdown][concurrent]") {
    const infra::Config cfg = makeLineConfig(1);
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    sim::SimClock clock(1.0);
    concurrency::ChangeSignal freed;
    sim::Fleet fleet(cfg, network, clock, stop, freed);
    fleet.start();

    core::Order order;
    order.id = core::OrderId("o0");
    order.restaurantNode = core::NodeId("n2");
    order.deliveryNode = core::NodeId("n0");
    REQUIRE(fleet.couriers().front()->tryAssign(order));
    std::this_thread::sleep_for(50ms);
    stop.requestStop();
    clock.wakeAll();

    auto joined = std::async(std::launch::async, [&] { fleet.join(); });
    REQUIRE(joined.wait_for(2s) == std::future_status::ready);
}
