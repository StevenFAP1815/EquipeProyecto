#include <delivery/sim/OrderGenerator.hpp>

#include <delivery/core/Rng.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace delivery::sim {

namespace {

// Probabilidad de que un tick genere una rafaga en lugar de un solo pedido.
// Ajustable empiricamente (DP.10). Bajo para que la media de pedidos por
// tick sea cercana a 1 cuando no hay rafaga.
constexpr double kBurstProbability = 0.05;

}  // namespace

OrderGenerator::OrderGenerator(const infra::Config& config,
                               concurrency::OrderBook& book,
                               concurrency::ShutdownToken& stop,
                               std::atomic<std::uint64_t>& orderIdCounter)
    : config_(config),
      book_(book),
      stop_(stop),
      orderIdCounter_(orderIdCounter),
      rng_(core::deriveSeed(config.orders.seed, /*streamId=*/0)),
      intervalDist_(
          // media = meanIntervalMs milisegundos simulados.
          // exponential_distribution recibe lambda = 1/media.
          1.0 / static_cast<double>(config.orders.meanIntervalMs)),
      burstSizeDist_(1, config.orders.burstMax),
      burstProbDist_(0.0, 1.0),
      pickRestaurantDist_(0, config.restaurants.empty()
                                ? 0
                                : config.restaurants.size() - 1),
      pickNodeDist_(0, config.nodes.empty() ? 0 : config.nodes.size() - 1) {}

core::OrderId OrderGenerator::nextOrderId() {
    const std::uint64_t n = orderIdCounter_.fetch_add(1, std::memory_order_relaxed);
    return core::OrderId("o" + std::to_string(n));
}

std::optional<core::Order> OrderGenerator::generateOne(std::int64_t nowMs) {
    if (config_.restaurants.empty() || config_.nodes.empty()) {
        return std::nullopt;
    }

    const auto& rest = config_.restaurants[pickRestaurantDist_(rng_)];
    const auto& node = config_.nodes[pickNodeDist_(rng_)];

    core::Order o;
    o.id           = nextOrderId();
    o.restaurant   = rest.id;
    o.deliveryNode = node.id;
    o.createdAtMs  = nowMs;
    o.state        = core::OrderState::Created;
    return o;
}

bool OrderGenerator::sleepUntilNextTick(std::int64_t& simulatedNowMs) {
    // Cuanto tiempo simulado dormir en este tick.
    // La distribucion exponencial modela llegadas tipo Poisson.
    const double simulatedSleepMs = intervalDist_(rng_);

    // Conversion a tiempo real usando timeScale (segundos simulados por
    // segundo real). Escalamos a microsegundos para no perder precision con
    // timeScale altos.
    //
    //   real_sleep_us = simulatedSleepMs * 1000 / timeScale
    const double realSleepUs =
        simulatedSleepMs * 1000.0 / config_.simulation.timeScale;

    // Dormimos en pequenos incrementos para reaccionar a shutdown. Esto NO es
    // polling: es el paso del tiempo simulado, permitido por la seccion 4.
    //
    // Dormimos en incrementos de hasta 50 ms reales.
    constexpr double kMaxSliceUs = 50'000.0;
    double remainingUs = realSleepUs;

    while (remainingUs > 0.0) {
        if (stop_.stopRequested()) {
            return false;
        }
        const double sliceUs = std::min(remainingUs, kMaxSliceUs);
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<std::int64_t>(sliceUs)));
        remainingUs -= sliceUs;
    }

    // Avanza el reloj simulado.
    simulatedNowMs += static_cast<std::int64_t>(simulatedSleepMs);
    return !stop_.stopRequested();
}

void OrderGenerator::run() {
    std::int64_t simulatedNowMs = 0;
    const std::int64_t durationMs =
        static_cast<std::int64_t>(config_.simulation.durationS) * 1000;

    while (!stop_.stopRequested()) {
        // Condicion de fin por duracion (si durationS > 0).
        if (durationMs > 0 && simulatedNowMs >= durationMs) {
            break;
        }

        // Cuantos pedidos crear en este tick.
        int count = 1;
        if (burstProbDist_(rng_) < kBurstProbability) {
            count = burstSizeDist_(rng_);
        }

        // Generar y empujar.
        for (int i = 0; i < count && !stop_.stopRequested(); ++i) {
            auto maybeOrder = generateOne(simulatedNowMs);
            if (!maybeOrder) {
                break;
            }
            // Si el libro esta lleno, tryPush devuelve false y el rechazo
            // se contabiliza dentro del OrderBook. El generador no necesita
            // saber el motivo.
            (void)book_.tryPush(std::move(*maybeOrder));
        }

        // Dormir hasta el proximo tick.
        if (!sleepUntilNextTick(simulatedNowMs)) {
            break;
        }
    }
}

}  // namespace delivery::sim