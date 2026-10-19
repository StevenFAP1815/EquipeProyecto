#include <delivery/sim/Courier.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

namespace delivery::sim {

namespace {

// Numero de pasos de interpolacion por segmento. 20 es suficiente para que
// el render (Etapa 4) vea movimiento fluido.
constexpr int kStepsPerSegment = 20;

}  // namespace

Courier::Courier(core::CourierId id,
                std::shared_ptr<const core::StreetNetwork> network,
                SimClock& clock,
                concurrency::ShutdownToken& stop,
                double speedKmh,
                core::NodeId startNode)
    : id_(std::move(id)),
      network_(std::move(network)),
      clock_(clock),
      stop_(stop),
      speedKmh_(speedKmh),
      startNode_(std::move(startNode)) {
    // Posicion inicial: coordenadas del startNode.
    const auto coords = network_->nodeLatLon(startNode_);
    if (coords) {
        position_ = *coords;
    }
    lastIdleMs_ = clock_.nowMs();
}

bool Courier::setTarget(const core::NodeId& target) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != CourierState::Idle) {
            return false;
        }
        pendingTarget_ = target;
        state_ = CourierState::MovingToRestaurant;
    }
    cv_.notify_one();
    return true;
}

CourierState Courier::state() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

core::LatLon Courier::position() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return position_;
}

std::int64_t Courier::lastIdleMs() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastIdleMs_;
}

void Courier::setPosition(const core::LatLon& pos) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    position_ = pos;
}

void Courier::run() {
    while (!stop_.stopRequested()) {
        // Esperar a que haya un target o hasta shutdown.
        core::NodeId target;
        {
            std::unique_lock<std::mutex> lock(stateMutex_);
            cv_.wait(lock, [&] {
                return pendingTarget_.has_value() || stop_.stopRequested();
            });
            if (stop_.stopRequested()) {
                break;
            }
            if (!pendingTarget_) {
                continue;
            }
            target = *pendingTarget_;
            pendingTarget_.reset();
        }

        // Calcular ruta (fuera del lock).
        const auto route = core::findShortestPath(*network_, startNode_, target);
        if (route) {
            doMovement(*route);
        }

        // Volver a Idle.
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = CourierState::Idle;
            lastIdleMs_ = clock_.nowMs();
        }
    }
}

void Courier::doMovement(const core::RouteResult& route) {
    if (route.path.size() < 2) {
        // Ruta trivial o vacia: ya estamos en el destino.
        return;
    }

    // Velocidad en metros por milisegundo simulado.
    // speedKmh [km/h] -> m/s -> m/ms.
    const double speedMetersPerMs = speedKmh_ / 3600.0;

    for (std::size_t i = 0; i + 1 < route.path.size(); ++i) {
        if (stop_.stopRequested()) return;

        const auto aOpt = network_->nodeLatLon(route.path[i]);
        const auto bOpt = network_->nodeLatLon(route.path[i + 1]);
        if (!aOpt || !bOpt) continue;

        const auto edgeLen = network_->edgeLength(route.path[i], route.path[i + 1]);
        if (!edgeLen) continue;

        const double travelMs = *edgeLen / speedMetersPerMs;
        const std::int64_t t0 = clock_.nowMs();

        for (int s = 1; s <= kStepsPerSegment; ++s) {
            if (stop_.stopRequested()) return;

            const double alpha = static_cast<double>(s) / kStepsPerSegment;

            core::LatLon p;
            p.lat = aOpt->lat + (bOpt->lat - aOpt->lat) * alpha;
            p.lon = aOpt->lon + (bOpt->lon - aOpt->lon) * alpha;
            setPosition(p);

            const std::int64_t targetMs =
                t0 + static_cast<std::int64_t>(travelMs * alpha);
            if (!clock_.waitUntilMs(targetMs, stop_)) {
                return;
            }
        }
    }
}

}  // namespace delivery::sim