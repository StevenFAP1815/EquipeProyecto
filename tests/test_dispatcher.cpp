#include <catch2/catch_test_macros.hpp>

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Dispatcher.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace delivery;
using namespace std::chrono_literals;

namespace {
infra::Config makeConfig(int couriers, int quoteTimeoutMs, int acceptTimeoutMs) {
    infra::Config cfg;
    cfg.nodes = {
        core::Node{core::NodeId("n0"), -17.7600, -63.2000},
        core::Node{core::NodeId("n1"), -17.7600, -63.1980},
        core::Node{core::NodeId("n2"), -17.7600, -63.1960},
        core::Node{core::NodeId("n3"), -17.7600, -63.1940},
        core::Node{core::NodeId("n4"), -17.7620, -63.1940},
        core::Node{core::NodeId("n5"), -17.7700, -63.1800},
    };
    cfg.streets = {
        core::Street{core::StreetId("s0"), core::NodeId("n0"), core::NodeId("n1"), false},
        core::Street{core::StreetId("s1"), core::NodeId("n1"), core::NodeId("n2"), false},
        core::Street{core::StreetId("s2"), core::NodeId("n2"), core::NodeId("n3"), false},
        core::Street{core::StreetId("s3"), core::NodeId("n3"), core::NodeId("n4"), true},
    };
    cfg.fleet.couriers = couriers;
    cfg.fleet.speedKmh = 30.0;
    cfg.fleet.startNode = core::NodeId("n0");
    cfg.dispatch.quoteTimeoutMs = quoteTimeoutMs;
    cfg.dispatch.acceptTimeoutMs = acceptTimeoutMs;
    return cfg;
}

struct Harness {
    infra::Config cfg;
    std::shared_ptr<const core::StreetNetwork> network;
    concurrency::ShutdownToken stop;
    concurrency::OrderBook book{100};
    concurrency::ChangeSignal freed;
    sim::SimClock clock{5000.0};
    std::unique_ptr<sim::Fleet> fleet;
    std::unique_ptr<sim::Dispatcher> dispatcher;
    std::thread dispatcherThread;

    Harness(infra::Config config, sim::Dispatcher::Estimator estimator = nullptr)
        : cfg(std::move(config)),
          network(core::StreetNetwork::build(cfg.nodes, cfg.streets)) {
        fleet = std::make_unique<sim::Fleet>(cfg, network, clock, stop, freed);
        dispatcher = std::make_unique<sim::Dispatcher>(
            cfg, network, book, *fleet, clock, freed, stop, std::move(estimator));
        fleet->start();
        dispatcherThread = std::thread([this] { dispatcher->run(); });
    }

    ~Harness() {
        stop.requestStop();
        clock.wakeAll();
        book.wakeAll();
        freed.wakeAll();
        dispatcherThread.join();
        fleet->join();
    }

    void pushOrder(const char* id, const char* restaurantNode, const char* deliveryNode) {
        core::Order o;
        o.id = core::OrderId(id);
        o.restaurantNode = core::NodeId(restaurantNode);
        o.deliveryNode = core::NodeId(deliveryNode);
        o.createdAtMs = clock.nowMs();
        o.readyAtMs = o.createdAtMs;
        REQUIRE(book.tryPush(std::move(o)));
    }
};

bool eventually(const std::function<bool()>& condition) {
    const auto limit = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < limit) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return condition();
}
}

TEST_CASE("Dispatcher: asigna y se entrega un pedido alcanzable", "[dispatcher]") {
    Harness h(makeConfig(3, 200, 600'000));
    h.pushOrder("o1", "n1", "n3");

    REQUIRE(eventually([&] { return h.fleet->deliveredCount() == 1; }));
    REQUIRE(h.dispatcher->unreachableCount() == 0);
    REQUIRE(h.dispatcher->noCourierCount() == 0);
}

TEST_CASE("Dispatcher: rechaza con unreachable lo que no se puede entregar por las calles",
          "[dispatcher]") {
    Harness h(makeConfig(2, 200, 600'000));
    h.pushOrder("o1", "n1", "n5");
    h.pushOrder("o2", "n1", "n4");

    REQUIRE(eventually([&] { return h.dispatcher->unreachableCount() == 2; }));
    REQUIRE(h.fleet->deliveredCount() == 0);
}

TEST_CASE("Dispatcher: una estimacion que lanza una excepcion se cuenta como error y no tumba el programa",
          "[dispatcher][concurrent]") {
    const sim::Dispatcher::Estimator alwaysFails = [](const sim::Courier&, sim::QuoteRequest) -> std::int64_t {
        throw std::runtime_error("estimacion fallida a proposito");
    };
    Harness h(makeConfig(2, 200, 1000), alwaysFails);
    h.pushOrder("o1", "n1", "n3");

    REQUIRE(eventually([&] { return h.dispatcher->noCourierCount() == 1; }));
    REQUIRE(h.dispatcher->quoteErrorCount() >= 2);
    REQUIRE(h.fleet->deliveredCount() == 0);
}

TEST_CASE("Dispatcher: ignora las estimaciones que llegan despues de quoteTimeoutMs",
          "[dispatcher][concurrent]") {
    const sim::Dispatcher::Estimator c0IsSlow = [](const sim::Courier& courier, sim::QuoteRequest) -> std::int64_t {
        if (courier.id() == core::CourierId("c0")) {
            std::this_thread::sleep_for(300ms);
            return 1;
        }
        return 1000;
    };
    Harness h(makeConfig(2, 50, 600'000), c0IsSlow);
    h.pushOrder("o1", "n1", "n3");

    REQUIRE(eventually([&] { return h.fleet->deliveredCount() == 1; }));
    REQUIRE(h.dispatcher->lateQuoteCount() >= 1);
    REQUIRE(h.fleet->findCourier(core::CourierId("c1"))->deliveredCount() == 1);
    REQUIRE(h.fleet->findCourier(core::CourierId("c0"))->deliveredCount() == 0);
}

TEST_CASE("Dispatcher: el numero de candidatos depende del hardware y es al menos 2",
          "[dispatcher]") {
    Harness h(makeConfig(1, 200, 600'000));
    const std::size_t hardware = std::thread::hardware_concurrency();

    REQUIRE(h.dispatcher->maxCandidates() >= 2);
    REQUIRE(h.dispatcher->maxCandidates() == std::max<std::size_t>(2, hardware));
}
