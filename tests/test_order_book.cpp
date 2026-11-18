#include <catch2/catch_test_macros.hpp>

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/Rng.hpp>
#include <delivery/sim/OrderGenerator.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using namespace delivery;
using namespace std::chrono_literals;

namespace {
core::Order makeOrder(const std::string& id) {
    core::Order o;
    o.id = core::OrderId(id);
    o.restaurant = core::RestaurantId("r0");
    o.deliveryNode = core::NodeId("n0");
    o.createdAtMs = 0;
    o.state = core::OrderState::Created;
    return o;
}
}

TEST_CASE("OrderBook: tryPush acepta hasta maxPending y luego rechaza",
          "[orderbook]") {
    concurrency::OrderBook book(10);

    for (int i = 0; i < 10; ++i) {
        REQUIRE(book.tryPush(makeOrder("o" + std::to_string(i))));
    }
    REQUIRE(book.size() == 10);
    REQUIRE(book.rejectedCount() == 0);

    REQUIRE_FALSE(book.tryPush(makeOrder("o10")));
    REQUIRE(book.size() == 10);
    REQUIRE(book.rejectedCount() == 1);

    REQUIRE_FALSE(book.tryPush(makeOrder("o11")));
    REQUIRE(book.rejectedCount() == 2);
}

TEST_CASE("OrderBook: waitAndPop respeta FIFO", "[orderbook]") {
    concurrency::OrderBook book(10);
    concurrency::ShutdownToken stop;

    REQUIRE(book.tryPush(makeOrder("o1")));
    REQUIRE(book.tryPush(makeOrder("o2")));
    REQUIRE(book.tryPush(makeOrder("o3")));

    auto p1 = book.waitAndPop(stop);
    REQUIRE(p1.has_value());
    REQUIRE(p1->id == core::OrderId("o1"));

    auto p2 = book.waitAndPop(stop);
    REQUIRE(p2.has_value());
    REQUIRE(p2->id == core::OrderId("o2"));

    auto p3 = book.waitAndPop(stop);
    REQUIRE(p3.has_value());
    REQUIRE(p3->id == core::OrderId("o3"));

    REQUIRE(book.size() == 0);
}

TEST_CASE("OrderBook: productor y consumidor no pierden pedidos",
          "[orderbook][concurrent]") {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 500;
    constexpr std::size_t kMaxPending = 50;

    concurrency::OrderBook book(kMaxPending);
    concurrency::ShutdownToken stop;
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> nextId{0};

    std::thread consumer([&] {
        while (true) {
            auto o = book.waitAndPop(stop);
            if (!o) return;
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                const std::uint64_t n = nextId.fetch_add(1, std::memory_order_relaxed);
                core::Order o = makeOrder("o" + std::to_string(n));
                if (book.tryPush(std::move(o))) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                } else {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : producers) t.join();

    std::this_thread::sleep_for(100ms);
    stop.requestStop();
    book.wakeAll();
    consumer.join();

    const std::uint64_t total = kProducers * kPerProducer;
    REQUIRE(accepted + rejected == total);
    REQUIRE(accepted == consumed + book.size());
    REQUIRE(consumed <= accepted);
}

TEST_CASE("OrderBook: wakeAll despierta a waitAndPop tras shutdown",
          "[orderbook][concurrent]") {
    concurrency::OrderBook book(10);
    concurrency::ShutdownToken stop;
    std::atomic<bool> consumerFinished{false};

    std::thread consumer([&] {
        while (true) {
            auto o = book.waitAndPop(stop);
            if (!o) {
                consumerFinished.store(true, std::memory_order_release);
                return;
            }
        }
    });

    std::this_thread::sleep_for(50ms);

    stop.requestStop();
    book.wakeAll();

    consumer.join();
    REQUIRE(consumerFinished.load(std::memory_order_acquire));
}

TEST_CASE("ShutdownToken: requestStop es idempotente", "[shutdown]") {
    concurrency::ShutdownToken stop;
    REQUIRE_FALSE(stop.stopRequested());

    stop.requestStop();
    REQUIRE(stop.stopRequested());

    stop.requestStop();
    REQUIRE(stop.stopRequested());
}

TEST_CASE("ShutdownToken: waitForStop retorna tras requestStop",
          "[shutdown][concurrent]") {
    concurrency::ShutdownToken stop;
    std::atomic<bool> woken{false};

    std::thread waiter([&] {
        stop.waitForStop();
        woken.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(50ms);
    REQUIRE_FALSE(woken.load(std::memory_order_acquire));

    stop.requestStop();
    waiter.join();
    REQUIRE(woken.load(std::memory_order_acquire));
}

TEST_CASE("deriveSeed: es determinista y distingue flujos",
          "[generator][reproducible]") {
    const std::uint64_t seed = 42;
    const std::uint64_t s0a = core::deriveSeed(seed, 0);
    const std::uint64_t s0b = core::deriveSeed(seed, 0);
    REQUIRE(s0a == s0b);

    const std::uint64_t s1 = core::deriveSeed(seed, 1);
    REQUIRE(s0a != s1);

    const std::uint64_t s2 = core::deriveSeed(seed + 1, 0);
    REQUIRE(s0a != s2);
}

TEST_CASE("Order: tipo seguro de copiar y mover", "[order][type]") {
    static_assert(std::is_copy_constructible<core::Order>::value,
                  "Order debe ser copiable");
    static_assert(std::is_copy_assignable<core::Order>::value,
                  "Order debe ser asignable por copia");
    static_assert(std::is_move_constructible<core::Order>::value,
                  "Order debe ser movible");
    static_assert(std::is_move_assignable<core::Order>::value,
                  "Order debe ser asignable por movimiento");
    static_assert(std::is_nothrow_move_constructible<core::Order>::value,
                  "Order debe ser nothrow movible");

    core::Order o1 = makeOrder("o1");
    core::Order o2 = o1;
    REQUIRE(o2.id == o1.id);

    core::Order o3 = std::move(o1);
    REQUIRE(o3.id == core::OrderId("o1"));
}

TEST_CASE("OrderBook: estres con muchos productores y consumidores",
          "[orderbook][concurrent][stress]") {
    constexpr int kProducers = 8;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 200;
    constexpr std::size_t kMaxPending = 100;

    concurrency::OrderBook book(kMaxPending);
    concurrency::ShutdownToken stop;
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> nextId{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (true) {
                auto o = book.waitAndPop(stop);
                if (!o) return;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                const std::uint64_t n = nextId.fetch_add(1, std::memory_order_relaxed);
                core::Order o = makeOrder("o" + std::to_string(n));
                if (book.tryPush(std::move(o))) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                } else {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : producers) t.join();

    std::this_thread::sleep_for(200ms);
    stop.requestStop();
    book.wakeAll();

    for (auto& t : consumers) t.join();

    const std::uint64_t total = kProducers * kPerProducer;
    REQUIRE(accepted + rejected == total);
    REQUIRE(accepted == consumed + book.size());
}

TEST_CASE("OrderBook: popNext saca urgentes primero y respeta el envejecimiento",
          "[orderbook][urgent]") {
    concurrency::OrderBook book(10);

    core::Order normal = makeOrder("o1");
    normal.createdAtMs = 0;
    core::Order urgent = makeOrder("o2");
    urgent.createdAtMs = 500;
    urgent.urgent = true;
    REQUIRE(book.tryPush(std::move(normal)));
    REQUIRE(book.tryPush(std::move(urgent)));

    auto first = book.popNext(600, 1000);
    REQUIRE(first.has_value());
    REQUIRE(first->id == core::OrderId("o2"));
    book.putBack(std::move(*first));

    auto aged = book.popNext(1200, 1000);
    REQUIRE(aged.has_value());
    REQUIRE(aged->id == core::OrderId("o1"));

    auto last = book.popNext(1200, 1000);
    REQUIRE(last.has_value());
    REQUIRE(last->id == core::OrderId("o2"));
    REQUIRE_FALSE(book.popNext(1200, 1000).has_value());
}

TEST_CASE("OrderBook: popNext desempata por id cuando tienen la misma hora",
          "[orderbook][urgent]") {
    concurrency::OrderBook book(10);
    REQUIRE(book.tryPush(makeOrder("o10")));
    REQUIRE(book.tryPush(makeOrder("o9")));

    auto first = book.popNext(0, 0);
    REQUIRE(first.has_value());
    REQUIRE(first->id == core::OrderId("o9"));
}

TEST_CASE("OrderBook: removeExpired y nextDeadlineMs", "[orderbook][urgent]") {
    concurrency::OrderBook book(10);
    core::Order old = makeOrder("o1");
    old.createdAtMs = 0;
    core::Order recent = makeOrder("o2");
    recent.createdAtMs = 800;
    REQUIRE(book.tryPush(std::move(old)));
    REQUIRE(book.tryPush(std::move(recent)));

    REQUIRE(book.nextDeadlineMs(900, 1000, 500) == std::optional<std::int64_t>(1000));

    const std::vector<core::OrderId> expired = book.removeExpired(1000, 1000);
    REQUIRE(expired.size() == 1);
    REQUIRE(expired[0] == core::OrderId("o1"));
    REQUIRE(book.size() == 1);

    REQUIRE(book.nextDeadlineMs(1000, 1000, 500) == std::optional<std::int64_t>(1300));
}
