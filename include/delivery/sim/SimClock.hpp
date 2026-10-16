#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace delivery::sim {

// ---------------------------------------------------------------------------
// SimClock: reloj simulado compartido.
//
// Modelo:
//   - El tiempo simulado avanza a `timeScale` segundos simulados por segundo
//     real. Con timeScale = 60, 1 segundo real = 60 segundos simulados.
//   - nowMs() calcula el tiempo simulado a partir de steady_clock. No bloquea.
//   - Un hilo ticker notifica la CV cada ~10 ms reales para despertar a los
//     que esperan con waitUntilMs.
//
// Uso:
//   SimClock clock(cfg.simulation.timeScale);
//   clock.start(stop);
//   ...
//   clock.waitUntilMs(clock.nowMs() + 500, stop);
//   ...
//   stop.requestStop();
//   clock.stop();
//
// Fila 15: los hilos esperan con predicado explicito; el ticker notifica.
// Fila 10: el unico dato mutable es el hilo ticker y su estado; el resto es
// inmutable tras el constructor.
// ---------------------------------------------------------------------------
class SimClock {
public:
    explicit SimClock(double timeScale);
    ~SimClock();

    SimClock(const SimClock&) = delete;
    SimClock& operator=(const SimClock&) = delete;

    // Tiempo simulado actual en milisegundos desde el inicio.
    // No bloquea. Thread-safe (solo lee estado inmutable).
    std::int64_t nowMs() const noexcept;

    // Bloquea hasta que nowMs() >= targetMs o hasta que se solicite shutdown.
    // Devuelve true si alcanzo el objetivo, false si se solicito shutdown.
    bool waitUntilMs(std::int64_t targetMs, concurrency::ShutdownToken& stop);

    // Arranca el hilo ticker. Llamado por main. Idempotente si ya arrancado
    // (segunda llamada no hace nada).
    void start(concurrency::ShutdownToken& stop);

    // Detiene el hilo ticker y lo joinea. Llamado por main tras requestStop.
    void stop();

    // Escala actual (segundos simulados por segundo real).
    double timeScale() const noexcept { return timeScale_; }

private:
    void tickerLoop(concurrency::ShutdownToken& stop);

    const double timeScale_;
    const std::chrono::steady_clock::time_point startReal_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread ticker_;
};

}  // namespace delivery::sim