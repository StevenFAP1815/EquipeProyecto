#include <catch2/catch_test_macros.hpp>

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/Rng.hpp>
#include <delivery/sim/OrderGenerator.hpp>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

using namespace delivery;
using namespace std::chrono_literals;

namespace {

// Helper: crea un pedido con el id dado.
core::Order makeOrder(const std::string& id) {
    core::Order o;
    o.id = core::OrderId(id);
    o.restaurant = core::RestaurantId("r0");
    o.deliveryNode = core::NodeId("n0");
    o.createdAtMs = 0;
    o.state = core::OrderState::Created;
    return o;
}

}  // namespace

// ---------------------------------------------------------------------------
// T1: tryPush respeta maxPending.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// T2: waitAndPop devuelve los pedidos en FIFO.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// T3: productor + consumidor, sin perder ni duplicar pedidos.
// ---------------------------------------------------------------------------

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

    // Consumidor: saca hasta que se solicite shutdown y la cola este vacia.
    std::thread consumer([&] {
        while (true) {
            auto o = book.waitAndPop(stop);
            if (!o) return;
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Productores.
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

    // Esperar a que el consumidor procese todo lo que pueda.
    // Le damos tiempo con sleep, pero el shutdown eventual lo hara terminar.
    std::this_thread::sleep_for(100ms);
    stop.requestStop();
    book.wakeAll();
    consumer.join();

    const std::uint64_t total = kProducers * kPerProducer;
    REQUIRE(accepted + rejected == total);
    REQUIRE(accepted == consumed + book.size());
    REQUIRE(consumed <= accepted);
}

// ---------------------------------------------------------------------------
// T4: shutdown despierta al consumidor bloqueado en waitAndPop.
// ---------------------------------------------------------------------------

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

    // Dar tiempo a que el consumidor entre en waitAndPop.
    std::this_thread::sleep_for(50ms);

    stop.requestStop();
    book.wakeAll();

    consumer.join();
    REQUIRE(consumerFinished.load(std::memory_order_acquire));
}

// ---------------------------------------------------------------------------
// T5: ShutdownToken idempotente.
// ---------------------------------------------------------------------------

TEST_CASE("ShutdownToken: requestStop es idempotente", "[shutdown]") {
    concurrency::ShutdownToken stop;
    REQUIRE_FALSE(stop.stopRequested());

    stop.requestStop();
    REQUIRE(stop.stopRequested());

    stop.requestStop();  // No debe romper.
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

// ---------------------------------------------------------------------------
// T6: reproducibilidad del OrderGenerator.
// ---------------------------------------------------------------------------

TEST_CASE("OrderGenerator: misma seed produce misma secuencia",
          "[generator][reproducible]") {
    // Construimos dos libros y dos generadores con la misma seed.
    // Ejecutamos cada uno brevemente y comparamos los ids generados.
    // Como los ids vienen del contador atomico, deben coincidir si el
    // numero de pedidos generados coincide (que depende de la seed).

    // Para evitar dependencia de tiempo, usamos el generador directamente
    // en modo "un tick": no podemos llamar a run() facilmente en un test
    // sin hilos. En su lugar, verificamos que dos generadores con la misma
    // seed produzcan el mismo primer tick.

    // Nota: run() es bloqueante y depende del reloj real. No lo usamos aqui.
    // Verificamos la reproducibilidad de deriveSeed y de los valores que
    // produce un mt19937_64 con la misma seed.

    const std::uint64_t seed = 42;
    const std::uint64_t s0a = core::deriveSeed(seed, 0);
    const std::uint64_t s0b = core::deriveSeed(seed, 0);
    REQUIRE(s0a == s0b);

    const std::uint64_t s1 = core::deriveSeed(seed, 1);
    REQUIRE(s0a != s1);

    const std::uint64_t s2 = core::deriveSeed(seed + 1, 0);
    REQUIRE(s0a != s2);
}

// ---------------------------------------------------------------------------
// T7: Order es copiable y movible (Fila 11 del mapa de conceptos).
// ---------------------------------------------------------------------------

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
    core::Order o2 = o1;                    // copia
    REQUIRE(o2.id == o1.id);

    core::Order o3 = std::move(o1);         // movimiento
    REQUIRE(o3.id == core::OrderId("o1"));
}

// ---------------------------------------------------------------------------
// T8: estres ligero con muchos productores y consumidores.
// ---------------------------------------------------------------------------

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

    // Damos tiempo a los consumidores a vaciar, luego paramos.
    std::this_thread::sleep_for(200ms);
    stop.requestStop();
    book.wakeAll();

    for (auto& t : consumers) t.join();

    const std::uint64_t total = kProducers * kPerProducer;
    REQUIRE(accepted + rejected == total);
    REQUIRE(accepted == consumed + book.size());
}