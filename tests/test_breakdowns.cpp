#include <catch2/catch_test_macros.hpp>

#include <delivery/infra/EventLog.hpp>

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <shared_mutex>
#include <thread>

using namespace delivery;
using namespace std::chrono_literals;

namespace {
infra::Config makeConfig(int couriers, double breakdownProbability) {
    infra::Config cfg;
    cfg.nodes = {
        core::Node{core::NodeId("n0"), -17.7600, -63.2000},
        core::Node{core::NodeId("n1"), -17.7600, -63.1980},
        core::Node{core::NodeId("n2"), -17.7600, -63.1960},
        core::Node{core::NodeId("n3"), -17.7600, -63.1940},
    };
    cfg.streets = {
        core::Street{core::StreetId("s0"), core::NodeId("n0"), core::NodeId("n1"), false},
        core::Street{core::StreetId("s1"), core::NodeId("n1"), core::NodeId("n2"), false},
        core::Street{core::StreetId("s2"), core::NodeId("n2"), core::NodeId("n3"), false},
    };
    cfg.restaurants = {
        core::Restaurant{core::RestaurantId("r0"), "R0", core::NodeId("n1"), 2, {0, 0}},
    };
    cfg.fleet.couriers = couriers;
    cfg.fleet.bagCapacity = 1;
    cfg.fleet.speedKmh = 30.0;
    cfg.fleet.startNode = core::NodeId("n0");
    cfg.incidents.breakdownProbability = breakdownProbability;
    cfg.orders.seed = 42;
    return cfg;
}

struct FleetHarness {
    infra::Config cfg;
    std::shared_ptr<const core::StreetNetwork> network;
    concurrency::ShutdownToken stop;
    concurrency::ChangeSignal freed;
    infra::EventLog log;
    sim::SimClock clock{500.0};
    std::unique_ptr<sim::Fleet> fleet;

    explicit FleetHarness(infra::Config config)
        : cfg(std::move(config)),
          network(core::StreetNetwork::build(cfg.nodes, cfg.streets)) {
        fleet = std::make_unique<sim::Fleet>(cfg, network, clock, stop, freed, log);
        fleet->start();
    }

    ~FleetHarness() {
        shutdown();
    }

    void shutdown() {
        stop.requestStop();
        clock.wakeAll();
        freed.wakeAll();
        fleet->join();
    }

    sim::Courier& courier(const char* id) {
        return *fleet->findCourier(core::CourierId(id));
    }

    void assign(const char* courierId, const char* orderId) {
        core::Order o;
        o.id = core::OrderId(orderId);
        o.restaurant = core::RestaurantId("r0");
        o.restaurantNode = core::NodeId("n1");
        o.deliveryNode = core::NodeId("n3");
        o.readyAtMs = clock.nowMs();
        REQUIRE(courier(courierId).tryAssign(o));
    }

    std::uint64_t ordersInBagsPlusDelivered() const {
        std::shared_lock<std::shared_mutex> snapshot(fleet->transferMutex());
        std::uint64_t total = 0;
        for (const auto& c : fleet->couriers()) {
            total += c->ordersInBag() + c->deliveredCount();
        }
        return total;
    }
};

bool eventually(const std::function<bool()>& condition) {
    const auto limit = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < limit) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return condition();
}
}

TEST_CASE("Averias: el pedido pasa a otro repartidor sin estar nunca en dos bolsas ni en ninguna",
          "[breakdown][concurrent]") {
    FleetHarness h(makeConfig(3, 1.0));
    h.assign("c0", "o1");

    bool alwaysExactlyOne = true;
    const auto limit = std::chrono::steady_clock::now() + 10s;
    while (h.fleet->deliveredCount() < 1 && std::chrono::steady_clock::now() < limit) {
        if (h.ordersInBagsPlusDelivered() != 1) {
            alwaysExactlyOne = false;
        }
    }

    REQUIRE(alwaysExactlyOne);
    REQUIRE(h.fleet->deliveredCount() == 1);
    REQUIRE(h.fleet->breakdownCount() >= 1);
    REQUIRE(h.fleet->reassignedCount() == 1);
    REQUIRE(h.courier("c0").deliveredCount() == 0);
}

TEST_CASE("Averias: dos averias al mismo tiempo no bloquean el sistema",
          "[breakdown][concurrent]") {
    FleetHarness h(makeConfig(4, 1.0));
    h.assign("c0", "o1");
    h.assign("c1", "o2");

    REQUIRE(eventually([&] { return h.fleet->deliveredCount() == 2; }));
    REQUIRE(h.fleet->breakdownCount() >= 2);
    REQUIRE(h.fleet->reassignedCount() == 2);
}

TEST_CASE("Averias: el apagado termina aunque un repartidor espere un rescate que no llega",
          "[breakdown][shutdown][concurrent]") {
    FleetHarness h(makeConfig(1, 1.0));
    h.assign("c0", "o1");
    REQUIRE(eventually([&] { return h.courier("c0").state() == sim::CourierState::BrokenDown; }));

    auto finished = std::async(std::launch::async, [&] { h.shutdown(); });
    REQUIRE(finished.wait_for(2s) == std::future_status::ready);
    REQUIRE(h.courier("c0").ordersInBag() == 1);
}

TEST_CASE("Averias: si todos se averian a la vez, cada uno se repara y entrega lo suyo",
          "[breakdown][concurrent]") {
    FleetHarness h(makeConfig(2, 1.0));
    h.assign("c0", "o1");
    h.assign("c1", "o2");

    REQUIRE(eventually([&] { return h.fleet->deliveredCount() == 2; }));
    REQUIRE(h.fleet->breakdownCount() == 2);
}

TEST_CASE("Averias: con probabilidad 0 nunca hay averias", "[breakdown]") {
    FleetHarness h(makeConfig(2, 0.0));
    h.assign("c0", "o1");

    REQUIRE(eventually([&] { return h.fleet->deliveredCount() == 1; }));
    REQUIRE(h.fleet->breakdownCount() == 0);
    REQUIRE(h.courier("c0").deliveredCount() == 1);
}
