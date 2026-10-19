#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>
#include <delivery/sim/SimClock.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace delivery::sim {
using core::CourierId;
// Estado del repartidor.
enum class CourierState {
    Idle,                 // sin trabajo, esperando
    MovingToRestaurant,   // yendo a recoger un pedido
    PickingUp,            // en el restaurante, esperando preparacion
    Delivering,           // llevando un pedido al cliente
    BrokenDown            // averiado (Etapa 7)
};

inline const char* toString(CourierState s) noexcept {
    switch (s) {
        case CourierState::Idle:               return "idle";
        case CourierState::MovingToRestaurant: return "movingToRestaurant";
        case CourierState::PickingUp:          return "pickingUp";
        case CourierState::Delivering:         return "delivering";
        case CourierState::BrokenDown:         return "brokenDown";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Courier: actor concurrente (hilo propio).
//
// Modelo de uso:
//   Courier c(id, network, clock, stop, speedKmh, startNode);
//   std::thread t([&]{ c.run(); });
//   ...
//   c.setTarget(someNode);
//   ...
//   stop.requestStop();
//   t.join();
//
// Fila 1: el hilo se crea explicitamente y se joinea (via Fleet).
// Fila 10: state_, position_, pendingTarget_, currentPath_,
//   currentPathIndex_ estan TODOS bajo stateMutex_. No hay acceso sin lock.
// Fila 14: objeto monitor. position() y state() devuelven copias.
// ---------------------------------------------------------------------------
class Courier {
public:
    Courier(CourierId id,
            std::shared_ptr<const core::StreetNetwork> network,
            SimClock& clock,
            concurrency::ShutdownToken& stop,
            double speedKmh,
            core::NodeId startNode);

    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    // Bucle principal. Llamado desde el hilo del courier.
    void run();

    // Asigna un target al courier. Solo acepta si esta Idle.
    // Devuelve true si acepto, false si estaba ocupado.
    // Llamado desde main (o el despachador, Etapa 5+).
    bool setTarget(const core::NodeId& target);

    // Consultas thread-safe para el render y los tests.
    CourierId id() const noexcept { return id_; }
    CourierState state() const;
    core::LatLon position() const;

    // Tiempo simulado de la ultima transicion a Idle (para estadisticas).
    std::int64_t lastIdleMs() const;

private:
    // Mueve el courier a lo largo de la ruta, paso a paso.
    void doMovement(const core::RouteResult& route);

    // Actualiza la posicion bajo lock.
    void setPosition(const core::LatLon& pos);

    const CourierId id_;
    std::shared_ptr<const core::StreetNetwork> network_;
    SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    const double speedKmh_;
    const core::NodeId startNode_;

    // Estado mutable, protegido por stateMutex_.
    mutable std::mutex stateMutex_;
    std::condition_variable cv_;
    CourierState state_ = CourierState::Idle;
    core::LatLon position_;
    std::optional<core::NodeId> pendingTarget_;
    std::int64_t lastIdleMs_ = 0;
};

}  // namespace delivery::sim