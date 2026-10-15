#pragma once

#include <delivery/core/Types.hpp>

#include <string>
#include <utility>

namespace delivery::core {

// Rango de tiempo simulado en milisegundos. Refleja el arreglo [min, max] del
// JSON de configuracion (por ejemplo, prepTimeMs de un restaurante).
struct TimeRange {
    int minMs = 0;
    int maxMs = 0;
};

// Interseccion de la red de calles. La topologia vive en StreetNetwork (Etapa 3);
// aqui solo la representacion de datos.
struct Node {
    NodeId id;
    double lat = 0.0;
    double lon = 0.0;
};

// Segmento de calle entre dos nodos.
//
// Convencion: la direccion canonica del segmento es from -> to.
// Si oneWay == false, el segmento es transitable en ambos sentidos.
// Si oneWay == true, solo se puede circular de from a to (nunca de to a from).
struct Street {
    StreetId id;
    NodeId from;
    NodeId to;
    bool oneWay = false;
};

// Restaurante anclado a un nodo de la red.
//
// pickupSlots: numero maximo de repartidores que pueden estar recogiendo
//              comida en este restaurante al mismo tiempo (>= 1).
// prepTimeMs:  rango de tiempo simulado que tarda en prepararse un pedido.
struct Restaurant {
    RestaurantId id;
    std::string name;
    NodeId node;
    int pickupSlots = 1;
    TimeRange prepTimeMs;
};

}  // namespace delivery::core