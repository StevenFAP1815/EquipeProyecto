#pragma once

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/infra/Config.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <random>

namespace delivery::sim {

// ---------------------------------------------------------------------------
// OrderGenerator: hilo productor de pedidos.
//
// Genera pedidos continuamente con intervalos exponenciales (modelo de
// Poisson) y rafagas ocasionales de tamano variable. La generacion es
// reproducible: con la misma seed (config.orders.seed), produce la misma
// secuencia de pedidos.
//
// Modelo de uso:
//   OrderGenerator gen(config, book, stop, orderIdCounter);
//   std::thread t([&]{ gen.run(); });
//   ...
//   stop.requestStop();
//   t.join();
//
// El hilo se detiene cuando stop.stopRequested() devuelve true.
// ---------------------------------------------------------------------------
class OrderGenerator {
public:
    // orderIdCounter: contador atomico compartido por todos los generadores
    // (en Etapa 2 solo hay uno). Se usa para producir ids unicos "oN".
    OrderGenerator(const infra::Config& config,
                   concurrency::OrderBook& book,
                   concurrency::ShutdownToken& stop,
                   std::atomic<std::uint64_t>& orderIdCounter);

    OrderGenerator(const OrderGenerator&) = delete;
    OrderGenerator& operator=(const OrderGenerator&) = delete;

    // Bucle principal del generador. Llamado desde su hilo.
    // Retorna cuando stop.stopRequested() o cuando se alcanza durationS
    // simulados (si durationS > 0).
    void run();

private:
    // Genera un pedido. Devuelve nullopt si no se puede (por ejemplo, no hay
    // restaurantes configurados).
    std::optional<core::Order> generateOne(std::int64_t nowMs);

    // Espera hasta el proximo tick (tiempo simulado). Despierta si se
    // solicita shutdown.
    // Devuelve true si debe continuar, false si debe terminar.
    bool sleepUntilNextTick(std::int64_t& simulatedNowMs);

    // Construye el proximo id unico.
    core::OrderId nextOrderId();

    const infra::Config& config_;
    concurrency::OrderBook& book_;
    concurrency::ShutdownToken& stop_;
    std::atomic<std::uint64_t>& orderIdCounter_;

    std::mt19937_64 rng_;
    std::exponential_distribution<double> intervalDist_;
    std::uniform_int_distribution<int> burstSizeDist_;
    std::uniform_real_distribution<double> burstProbDist_;
    std::uniform_int_distribution<std::size_t> pickRestaurantDist_;
    std::uniform_int_distribution<std::size_t> pickNodeDist_;
};

}  // namespace delivery::sim