#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace delivery::concurrency {

// ---------------------------------------------------------------------------
// OrderBook: monitor del libro de pedidos pendientes.
//
// Fila 14 del mapa de conceptos: los datos compartidos (queue_) son
// inalcanzables sin su bloqueo (mutex_). La unica forma de acceder al deque
// interno es a traves de los metodos publicos, que toman el lock.
//
// Fila 15 del mapa de conceptos: los hilos que esperan un pedido lo hacen
// en waitAndPop, que usa una condition_variable con predicado explicito
// (!queue_.empty() || stop.stopRequested()), resistente a despertares
// espurios.
//
// Capacidad: el libro tiene un maximo configurable de pedidos en espera.
// tryPush devuelve false si el libro esta lleno (rechazo queueFull).
// ---------------------------------------------------------------------------
class OrderBook {
public:
    explicit OrderBook(std::size_t maxPending);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Intenta agregar un pedido al libro. El pedido se mueve (semantica de
    // movimiento, Fila 4). No bloquea. Devuelve true si fue aceptado, false
    // si el libro esta lleno (queueFull).
    bool tryPush(core::Order&& order);

    // Espera hasta que haya un pedido o hasta que se solicite shutdown.
    // Devuelve el pedido movido, o std::nullopt si se solicito shutdown y
    // el libro esta vacio.
    //
    // Uso tipico:
    //   while (auto o = book.waitAndPop(stop)) {
    //       // procesar *o
    //   }
    std::optional<core::Order> waitAndPop(ShutdownToken& stop);

    // Numero de pedidos actualmente en espera.
    std::size_t size() const;

    // Capacidad maxima configurada (inmutable tras el constructor).
    std::size_t capacity() const noexcept;

    // Numero total de pedidos rechazados por queueFull desde la creacion.
    std::uint64_t rejectedCount() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<core::Order> queue_;    // protegido por mutex_
    const std::size_t maxPending_;
    std::uint64_t rejectedCount_ = 0;  // protegido por mutex_
};

}  // namespace delivery::concurrency