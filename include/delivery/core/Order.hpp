#pragma once

#include <delivery/core/Types.hpp>

#include <cstdint>
#include <string>

namespace delivery::core {

// ---------------------------------------------------------------------------
// Estados del pedido.
//
// Orden de transicion valido:
//   Created -> Assigned -> PickedUp -> Delivered
//
// Estados finales (nunca se sale de ellos):
//   Delivered, Rejected, Pending
//
// Un pedido siempre termina en exactamente uno de los tres finales.
// ---------------------------------------------------------------------------
enum class OrderState {
    Created,
    Assigned,
    PickedUp,
    Delivered,
    Rejected,
    Pending
};

inline const char* toString(OrderState s) noexcept {
    switch (s) {
        case OrderState::Created:   return "created";
        case OrderState::Assigned:  return "assigned";
        case OrderState::PickedUp:  return "pickedUp";
        case OrderState::Delivered: return "delivered";
        case OrderState::Rejected:  return "rejected";
        case OrderState::Pending:   return "pending";
    }
    return "unknown";
}

inline bool isFinal(OrderState s) noexcept {
    return s == OrderState::Delivered
        || s == OrderState::Rejected
        || s == OrderState::Pending;
}

// ---------------------------------------------------------------------------
// Motivo de rechazo de un pedido.
//
// Los tres motivos posibles segun la seccion 4 del proyecto.
// ---------------------------------------------------------------------------
enum class RejectReason {
    None,
    QueueFull,
    Unreachable,
    NoCourier
};

inline const char* toString(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:        return "none";
        case RejectReason::QueueFull:   return "queueFull";
        case RejectReason::Unreachable: return "unreachable";
        case RejectReason::NoCourier:   return "noCourier";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Pedido.
//
// Struct plano, copiable y movible. Todos los miembros tienen semantica de
// valor correcta, por lo que el compilador genera las cinco funciones
// especiales por defecto, correctamente. No hay recursos que gestionar.
//
// Un pedido vive en un solo lugar a la vez. Se transfiere por movimiento
// (std::move) al OrderBook, a la bolsa de un repartidor, etc. Nunca se copia
// entre dos contenedores simultaneamente.
// ---------------------------------------------------------------------------
struct Order {
    OrderId id;                       // identificador unico
    RestaurantId restaurant;          // restaurante que prepara el pedido
    NodeId deliveryNode;              // nodo de entrega en la red de calles
    std::int64_t createdAtMs = 0;     // milisegundos simulados desde el inicio
    OrderState state = OrderState::Created;
    RejectReason rejectReason = RejectReason::None;  // valido si state == Rejected
    CourierId assignedCourier;        // vacio hasta que se asigna
};

}  // namespace delivery::core