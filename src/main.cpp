#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/sim/OrderGenerator.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

using namespace delivery;

// Hilo consumidor de prueba: saca pedidos del libro y los cuenta.
// En Etapa 2 NO procesa realmente los pedidos (no hay despachador ni
// repartidores todavia). Solo verifica que el flujo productor-consumidor
// funciona bajo concurrencia real.
void consumerLoop(concurrency::OrderBook& book,
                  concurrency::ShutdownToken& stop,
                  std::atomic<std::uint64_t>& consumedCount) {
    while (true) {
        auto maybeOrder = book.waitAndPop(stop);
        if (!maybeOrder) {
            // Shutdown solicitado y cola vacia.
            return;
        }
        // En Etapa 5 aqui vivira la logica del despachador.
        consumedCount.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

int main(int argc, char** argv) {
    using namespace delivery;

    if (argc < 2) {
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>]\n";
        return 1;
    }

    try {
        const infra::Config cfg = infra::loadConfigFromFile(argv[1]);

        // Recursos compartidos.
        concurrency::ShutdownToken stop;
        concurrency::OrderBook book(static_cast<std::size_t>(cfg.orders.maxPending));
        std::atomic<std::uint64_t> orderIdCounter{0};
        std::atomic<std::uint64_t> consumedCount{0};

        // Productor.
        sim::OrderGenerator generator(cfg, book, stop, orderIdCounter);

        // Lanzar hilos (Fila 1 del mapa de conceptos: creacion explicita,
        // join deliberado).
        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tConsumer([&book, &stop, &consumedCount] {
            consumerLoop(book, stop, consumedCount);
        });

        // Esperar la duracion simulada convertida a tiempo real.
        // Si durationS == 0, esperamos hasta que se solicite shutdown
        // externamente (senal, Etapa 9). En Etapa 2 no hay senales, asi que
        // solo salimos por duracion o por Ctrl+C del sistema operativo.
        //
        // Pausar aqui es representar el paso del tiempo simulado, permitido
        // por la seccion 4 del proyecto.
        if (cfg.simulation.durationS > 0) {
            const double realSeconds =
                static_cast<double>(cfg.simulation.durationS) /
                cfg.simulation.timeScale;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    static_cast<std::int64_t>(realSeconds * 1000.0)));
        } else {
            // Sin duracion, esperar indefinidamente (shutdown externo).
            stop.waitForStop();
        }

        // Solicitar parada.
        stop.requestStop();

        // Despertar al consumidor: waitAndPop esta esperando en su CV con el
        // predicado stop.stopRequested(). La notificacion del token no llega
        // a la CV del book (son distintas). Por eso notificamos manualmente
        // al book. Deuda tecnica documentada (ver DD-017).
        // No tenemos acceso a la CV interna del book desde fuera. En su
        // lugar, insertamos un pedido "centinela" no valido para forzar el
        // despertar. Alternativa mejor: exponer un metodo book.wakeAll().
        //
        // DECISION: anadimos book.wakeAll() al OrderBook (ver Paso 4.2).
        book.wakeAll();

        // Join deliberado de TODOS los hilos.
        tProducer.join();
        tConsumer.join();

        // Resumen parcial (Etapa 2). El resumen completo (seccion 6.3) llega
        // en Etapa 8.
        const std::uint64_t created  = orderIdCounter.load(std::memory_order_relaxed);
        const std::uint64_t consumed = consumedCount.load(std::memory_order_relaxed);
        const std::uint64_t rejected = book.rejectedCount();
        const std::uint64_t pending  = book.size();

        std::cout << "{"
                  << "\"created\": "   << created  << ", "
                  << "\"consumed\": "  << consumed << ", "
                  << "\"rejected\": "  << rejected << ", "
                  << "\"pending\": "   << pending
                  << "}\n";

        return 0;

    } catch (const infra::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 2;
    }
}