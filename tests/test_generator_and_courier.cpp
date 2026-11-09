#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <delivery/concurrency/ChangeSignal.hpp>
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

void waitUntilFree(const sim::Courier& courier) {
    while (!courier.isAvailable()) {
        std::this_thread::sleep_for(1ms);
    }
}

core::Order makeOrder(const char* id, const char* restaurantNode,
                      const char* deliveryNode, std::int64_t readyAtMs) {
    core::Order o;
    o.id = core::OrderId(id);
    o.restaurant = core::RestaurantId("r0");
    o.restaurantNode = core::NodeId(restaurantNode);
    o.deliveryNode = core::NodeId(deliveryNode);
    o.readyAtMs = readyAtMs;
    return o;
}

sim::PickupDeskMap makeDesks() {
    sim::PickupDeskMap desks;
    desks.emplace(core::RestaurantId("r0"), std::make_unique<concurrency::PickupDesk>(1));
    return desks;
}

infra::Config makeOneWayLine() {
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
    return cfg;
}

void stopCourier(concurrency::ShutdownToken& stop, sim::SimClock& clock,
                 sim::Courier& courier, std::thread& worker) {
    stop.requestStop();
    clock.wakeAll();
    courier.wake();
    worker.join();
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

TEST_CASE("Courier: recoge en el restaurante y entrega en el destino",
          "[courier]") {
    const infra::Config cfg = makeOneWayLine();
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    sim::SimClock clock(5000.0);
    const sim::PickupDeskMap desks = makeDesks();
    sim::Courier courier(core::CourierId("c0"), network, clock, stop, freed, desks, 1, 30.0,
                         core::NodeId("n0"));
    std::thread worker([&courier] { courier.run(); });

    const std::uint64_t versionBefore = freed.version();
    core::Order order = makeOrder("o1", "n1", "n3", 0);
    REQUIRE(courier.tryAssign(order));
    REQUIRE(courier.deliveryNodesInBag() == std::vector<core::NodeId>{core::NodeId("n3")});

    core::Order another = makeOrder("o2", "n1", "n2", 0);
    REQUIRE_FALSE(courier.tryAssign(another));

    waitUntilFree(courier);
    REQUIRE(courier.deliveredCount() == 1);
    REQUIRE(courier.currentNode() == core::NodeId("n3"));
    REQUIRE(courier.deliveryNodesInBag().empty());
    REQUIRE(freed.version() > versionBefore);

    const auto end = courier.position();
    REQUIRE_THAT(end.lat, WithinAbs(cfg.nodes[3].lat, 1e-9));
    REQUIRE_THAT(end.lon, WithinAbs(cfg.nodes[3].lon, 1e-9));

    stopCourier(stop, clock, courier, worker);
}

TEST_CASE("Courier: espera en el restaurante hasta que la comida esta lista",
          "[courier]") {
    const infra::Config cfg = makeOneWayLine();
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    sim::SimClock clock(5000.0);
    const sim::PickupDeskMap desks = makeDesks();
    sim::Courier courier(core::CourierId("c0"), network, clock, stop, freed, desks, 1, 30.0,
                         core::NodeId("n1"));
    std::thread worker([&courier] { courier.run(); });

    const std::int64_t readyAt = clock.nowMs() + 600'000;
    core::Order order = makeOrder("o1", "n1", "n2", readyAt);
    REQUIRE(courier.tryAssign(order));
    waitUntilFree(courier);

    REQUIRE(courier.lastIdleMs() >= readyAt);
    stopCourier(stop, clock, courier, worker);
}

TEST_CASE("Courier: el segundo pedido parte desde donde termino el primero",
          "[courier]") {
    const infra::Config cfg = makeOneWayLine();
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    sim::SimClock clock(500.0);
    const sim::PickupDeskMap desks = makeDesks();
    sim::Courier courier(core::CourierId("c0"), network, clock, stop, freed, desks, 1, 30.0,
                         core::NodeId("n0"));
    std::thread worker([&courier] { courier.run(); });

    core::Order first = makeOrder("o1", "n1", "n2", 0);
    REQUIRE(courier.tryAssign(first));
    waitUntilFree(courier);
    REQUIRE(courier.currentNode() == core::NodeId("n2"));

    const std::int64_t secondStart = clock.nowMs();
    core::Order second = makeOrder("o2", "n2", "n3", 0);
    REQUIRE(courier.tryAssign(second));
    waitUntilFree(courier);
    const std::int64_t secondMs = courier.lastIdleMs() - secondStart;

    const double segmentMeters = *network->edgeLength(core::NodeId("n2"), core::NodeId("n3"));
    const double segmentMs = segmentMeters / (30.0 / 3600.0);
    REQUIRE(secondMs < 2.0 * segmentMs);
    REQUIRE(courier.deliveredCount() == 2);

    stopCourier(stop, clock, courier, worker);
}

TEST_CASE("Courier: un pedido sin ruta posible cuenta como fallido y libera al repartidor",
          "[courier]") {
    const infra::Config cfg = makeOneWayLine();
    auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    sim::SimClock clock(5000.0);
    const sim::PickupDeskMap desks = makeDesks();
    sim::Courier courier(core::CourierId("c0"), network, clock, stop, freed, desks, 1, 30.0,
                         core::NodeId("n3"));
    std::thread worker([&courier] { courier.run(); });

    core::Order order = makeOrder("o1", "n0", "n1", 0);
    REQUIRE(courier.tryAssign(order));
    waitUntilFree(courier);

    REQUIRE(courier.failedCount() == 1);
    REQUIRE(courier.deliveredCount() == 0);
    stopCourier(stop, clock, courier, worker);
}

TEST_CASE("ChangeSignal: notify despierta a quien espera un cambio",
          "[change_signal][concurrent]") {
    concurrency::ChangeSignal signal;
    concurrency::ShutdownToken stop;
    const std::uint64_t seen = signal.version();

    std::thread notifier([&signal] {
        std::this_thread::sleep_for(20ms);
        signal.notify();
    });
    const bool changed = signal.waitForChange(
        seen, std::chrono::steady_clock::now() + 5s, stop);
    notifier.join();

    REQUIRE(changed);
    REQUIRE(signal.version() == seen + 1);
}
