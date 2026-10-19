#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/SimClock.hpp>

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace delivery::sim {

// ---------------------------------------------------------------------------
// Fleet: contenedor de repartidores y sus hilos.
//
// Modelo de uso:
//   Fleet fleet(cfg, network, clock, stop);
//   fleet.start();
//   ...
//   stop.requestStop();
//   fleet.join();
//
// Fila 1: creacion explicita de hilos con join deliberado.
// Fila 5: contenedor de hilos (std::vector<std::thread>).
// Fila 6: comparte la propiedad del StreetNetwork via shared_ptr<const>.
// ---------------------------------------------------------------------------
class Fleet {
public:
    Fleet(const infra::Config& cfg,
          std::shared_ptr<const core::StreetNetwork> network,
          SimClock& clock,
          concurrency::ShutdownToken& stop);

    ~Fleet();

    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    // Lanza un hilo por courier. Llamado una vez por main.
    void start();

    // Joinea todos los hilos. Llamado tras requestStop.
    void join();

    // Acceso a los couriers para render y despachador.
    const std::vector<std::unique_ptr<Courier>>& couriers() const noexcept {
        return couriers_;
    }

    std::size_t size() const noexcept { return couriers_.size(); }

    // Acceso a un courier por id. Devuelve nullptr si no existe.
    Courier* findCourier(const core::CourierId& id) noexcept;

private:
    std::vector<std::unique_ptr<Courier>> couriers_;
    std::vector<std::thread> threads_;
};

}  // namespace delivery::sim