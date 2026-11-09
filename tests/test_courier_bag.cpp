#include <catch2/catch_test_macros.hpp>

#include <delivery/infra/EventLog.hpp>

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/PickupDesk.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/SimClock.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <optional>
#include <shared_mutex>
#include <memory>
#include <thread>
#include <vector>

using namespace delivery;
using namespace std::chrono_literals;

namespace {
struct BagHarness {
    std::shared_ptr<const core::StreetNetwork> network;
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    sim::PickupDeskMap desks;
    std::shared_mutex transferMutex;
    infra::EventLog log;
    sim::SimClock clock{500.0};
    std::unique_ptr<sim::Courier> courier;
    std::thread worker;

    explicit BagHarness(int bagCapacity) {
        const std::vector<core::Node> nodes = {
            core::Node{core::NodeId("n0"), -17.7600, -63.2000},
            core::Node{core::NodeId("n1"), -17.7600, -63.1980},
            core::Node{core::NodeId("n2"), -17.7600, -63.1960},
            core::Node{core::NodeId("n3"), -17.7600, -63.1940},
        };
        const std::vector<core::Street> streets = {
            core::Street{core::StreetId("s0"), core::NodeId("n0"), core::NodeId("n1"), false},
            core::Street{core::StreetId("s1"), core::NodeId("n1"), core::NodeId("n2"), false},
            core::Street{core::StreetId("s2"), core::NodeId("n2"), core::NodeId("n3"), false},
        };
        network = core::StreetNetwork::build(nodes, streets);
        desks.emplace(core::RestaurantId("r0"), std::make_unique<concurrency::PickupDesk>(1));
        desks.emplace(core::RestaurantId("r1"), std::make_unique<concurrency::PickupDesk>(1));
        sim::CourierEnvironment environment{network, clock, stop, freed, desks, transferMutex, log,
            [](sim::Courier&) { return std::optional<std::future<bool>>{}; }};
        courier = std::make_unique<sim::Courier>(core::CourierId("c0"), environment,
                                                 sim::CourierSettings{bagCapacity, 30.0, 0.0, 0},
                                                 core::NodeId("n0"));
        worker = std::thread([this] { courier->run(); });
    }

    ~BagHarness() {
        stop.requestStop();
        clock.wakeAll();
        courier->wake();
        for (auto& entry : desks) {
            entry.second->wakeAll();
        }
        worker.join();
    }

    core::Order order(const char* id, const char* restaurant, const char* restaurantNode,
                      const char* deliveryNode, std::int64_t readyAtMs) {
        core::Order o;
        o.id = core::OrderId(id);
        o.restaurant = core::RestaurantId(restaurant);
        o.restaurantNode = core::NodeId(restaurantNode);
        o.deliveryNode = core::NodeId(deliveryNode);
        o.readyAtMs = readyAtMs;
        return o;
    }

    void waitUntilFree() {
        while (!courier->isAvailable()) {
            std::this_thread::sleep_for(1ms);
        }
    }
};
}

TEST_CASE("Bolsa: lleva varios pedidos del mismo restaurante y nunca supera la capacidad",
          "[courier][bag]") {
    BagHarness h(3);
    const std::int64_t now = h.clock.nowMs();

    core::Order o1 = h.order("o1", "r0", "n1", "n2", now);
    core::Order o2 = h.order("o2", "r0", "n1", "n3", now);
    core::Order o3 = h.order("o3", "r0", "n1", "n0", now);
    core::Order o4 = h.order("o4", "r0", "n1", "n2", now);
    REQUIRE(h.courier->tryAssign(o1));
    REQUIRE(h.courier->tryAssign(o2));
    REQUIRE(h.courier->tryAssign(o3));
    REQUIRE_FALSE(h.courier->tryAssign(o4));
    REQUIRE(h.courier->ordersInBag() == 3);

    std::size_t maxSeen = 0;
    while (!h.courier->isAvailable()) {
        maxSeen = std::max(maxSeen, h.courier->ordersInBag());
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(maxSeen <= 3);
    REQUIRE(h.courier->deliveredCount() == 3);
}

TEST_CASE("Bolsa: no mezcla pedidos de restaurantes distintos", "[courier][bag]") {
    BagHarness h(3);
    const std::int64_t now = h.clock.nowMs();

    core::Order fromR0 = h.order("o1", "r0", "n1", "n2", now);
    core::Order fromR1 = h.order("o2", "r1", "n2", "n3", now);
    REQUIRE(h.courier->tryAssign(fromR0));
    REQUIRE_FALSE(h.courier->canAccept(core::RestaurantId("r1")));
    REQUIRE_FALSE(h.courier->tryAssign(fromR1));

    h.waitUntilFree();
    REQUIRE(h.courier->deliveredCount() == 1);
}

TEST_CASE("Bolsa: un pedido que llega mientras va al restaurante se suma y se espera su comida",
          "[courier][bag]") {
    BagHarness h(2);
    const std::int64_t now = h.clock.nowMs();
    const std::int64_t lateReady = now + 300'000;

    core::Order first = h.order("o1", "r0", "n1", "n2", now);
    core::Order second = h.order("o2", "r0", "n1", "n3", lateReady);
    REQUIRE(h.courier->tryAssign(first));
    REQUIRE(h.courier->tryAssign(second));

    h.waitUntilFree();
    REQUIRE(h.courier->deliveredCount() == 2);
    REQUIRE(h.courier->lastIdleMs() >= lateReady);
}

TEST_CASE("Bolsa: no acepta pedidos nuevos una vez que salio del restaurante",
          "[courier][bag]") {
    BagHarness h(2);
    const std::int64_t now = h.clock.nowMs();

    core::Order first = h.order("o1", "r0", "n1", "n3", now);
    REQUIRE(h.courier->tryAssign(first));
    while (h.courier->state() != sim::CourierState::Delivering) {
        std::this_thread::sleep_for(1ms);
    }

    core::Order late = h.order("o2", "r0", "n1", "n2", now);
    REQUIRE_FALSE(h.courier->tryAssign(late));

    h.waitUntilFree();
    REQUIRE(h.courier->deliveredCount() == 1);
}
