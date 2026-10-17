#include <delivery/sim/Fleet.hpp>

#include <string>

namespace delivery::sim {

Fleet::Fleet(const infra::Config& cfg,
             std::shared_ptr<const core::StreetNetwork> network,
             SimClock& clock,
             concurrency::ShutdownToken& stop) {
    const int n = cfg.fleet.couriers;
    couriers_.reserve(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        const core::CourierId id("c" + std::to_string(i));
        couriers_.push_back(std::make_unique<Courier>(
            id,
            network,  // copia del shared_ptr
            clock,
            stop,
            cfg.fleet.speedKmh,
            cfg.fleet.startNode));
    }
}

Fleet::~Fleet() {
    // Contrato: join() debe haber sido llamado antes de destruir.
    // Si quedan hilos joinable, el destructor de std::thread llama a
    // std::terminate. Aqui solo verificamos y dejamos que el contrato
    // se cumpla.
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void Fleet::start() {
    threads_.reserve(couriers_.size());
    for (auto& c : couriers_) {
        // Capturamos el puntero crudo no propietario. La propiedad vive en
        // couriers_, que no se modifica tras start().
        Courier* ptr = c.get();
        threads_.emplace_back([ptr] {
            ptr->run();
        });
    }
}

void Fleet::join() {
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

Courier* Fleet::findCourier(const core::CourierId& id) noexcept {
    for (auto& c : couriers_) {
        if (c->id() == id) {
            return c.get();
        }
    }
    return nullptr;
}

}  // namespace delivery::sim