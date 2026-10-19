#pragma once

#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>

#include <optional>
#include <vector>

namespace delivery::core {

// Resultado de una busqueda de ruta.
struct RouteResult {
    std::vector<NodeId> path;  // secuencia de nodos, del origen al destino
    double totalMeters = 0.0;
};

// ---------------------------------------------------------------------------
// Dijkstra desde `from` a `to`.
//
// Devuelve nullopt si:
//   - `from` o `to` no existen en la red.
//   - No hay ruta (grafo desconectado).
//
// Si `from == to`, devuelve una ruta de un solo nodo con distancia 0.
//
// Complejidad: O((N + E) log N) con heap binario. Para N=300 y E=600, es
// trivial (~milisegundos).
//
// El algoritmo trabaja con indices internos (std::size_t) para evitar
// hashes de NodeId en el bucle caliente. Solo convierte al inicio y al final.
// ---------------------------------------------------------------------------
std::optional<RouteResult> findShortestPath(const StreetNetwork& network,
                                            const NodeId& from,
                                            const NodeId& to);

}  // namespace delivery::core