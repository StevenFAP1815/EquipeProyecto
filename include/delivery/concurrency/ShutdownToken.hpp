#pragma once

#include <condition_variable>
#include <mutex>

namespace delivery::concurrency {

// ---------------------------------------------------------------------------
// ShutdownToken: senal de parada cooperativa entre hilos.
//
// Modelo de uso:
//   - Un hilo "controlador" (main, signal handler) llama a requestStop()
//     cuando quiere que todos los hilos terminen.
//   - Los hilos "trabajadores" consultan stopRequested() periodicamente
//     o lo usan como parte del predicado de sus condition_variables.
//   - El hilo "controlador" puede bloquearse con waitForStop() hasta que
//     algun hilo active la senal (util si varios hilos pueden pedir parada).
//
// Garantias:
//   - requestStop() es idempotente: llamarlo varias veces es seguro.
//   - stopRequested() es noexcept y no bloquea (toma el mutex brevemente).
//   - Todos los hilos que esperan en waitForStop() despiertan cuando se
//     llama a requestStop().
//
// Fila 15 del mapa de conceptos: hilos que esperan una condicion y son
// despertados con predicado explicito, resistentes a despertares espurios.
// ---------------------------------------------------------------------------
class ShutdownToken {
public:
    ShutdownToken() = default;

    ShutdownToken(const ShutdownToken&) = delete;
    ShutdownToken& operator=(const ShutdownToken&) = delete;

    // Marca el apagado. Idempotente. Despierta a todos los hilos que esperan
    // en waitForStop().
    void requestStop();

    // Consulta sin bloqueo. Usada por los predicados de CVs externas.
    bool stopRequested() const noexcept;

    // Bloquea el hilo llamador hasta que se solicite el apagado.
    // Si ya fue solicitado, retorna inmediatamente.
    void waitForStop();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;  // protegido por mutex_
};

}  // namespace delivery::concurrency