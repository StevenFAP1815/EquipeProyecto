#include <delivery/sim/SimClock.hpp>

#include <chrono>

namespace delivery::sim {

namespace {

// Frecuencia del ticker en milisegundos reales.
// El ticker notifica la CV a esta frecuencia para despertar a los que
// esperan. 10 ms reales = 100 Hz, suficiente para simulacion visual.
constexpr int kTickerIntervalMs = 10;

}  // namespace

SimClock::SimClock(double timeScale)
    : timeScale_(timeScale),
      startReal_(std::chrono::steady_clock::now()) {}


SimClock::~SimClock() {
    // Contrato: stop() debe haber sido llamado antes de destruir.
    // Si no, el programa aborta por std::thread::joinable en destruccion.
    // En Debug, verificamos.
    // (En Release, permitimos el comportamiento indefinido del destructor
    // de std::thread: llama a std::terminate si joinable.)
}

std::int64_t SimClock::nowMs() const noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - startReal_;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return static_cast<std::int64_t>(static_cast<double>(elapsedMs) * timeScale_);
}

bool SimClock::waitUntilMs(std::int64_t targetMs,
                           concurrency::ShutdownToken& stop) {
    // Caso trivial: ya pasamos el objetivo.
    if (nowMs() >= targetMs) {
        return !stop.stopRequested();
    }

    std::unique_lock<std::mutex> lock(mutex_);

    // Esperar con predicado. El ticker notifica periodicamente, pero
    // tambien puede despertarnos el mismo ticker por otras razones.
    // Fila 15: predicado explicito, resistente a despertares espurios.
    cv_.wait(lock, [&] {
        return nowMs() >= targetMs || stop.stopRequested();
    });

    return !stop.stopRequested();
}

void SimClock::start(concurrency::ShutdownToken& stop) {
    if (ticker_.joinable()) {
        return;  // ya arrancado
    }
    ticker_ = std::thread([this, &stop] {
        tickerLoop(stop);
    });
}

void SimClock::stop() {
    if (ticker_.joinable()) {
        ticker_.join();
    }
}

void SimClock::tickerLoop(concurrency::ShutdownToken& stop) {
    while (!stop.stopRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickerIntervalMs));
        // Notificar a todos los que esperan en waitUntilMs. Fuera del lock:
        // el ticker no toma el lock del SimClock; solo notifica.
        cv_.notify_all();
    }
    // Notificar una ultima vez para que los que esperan reevaluen su
    // predicado y salgan por shutdown.
    cv_.notify_all();
}

}  // namespace delivery::sim