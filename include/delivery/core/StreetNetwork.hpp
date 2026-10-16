#pragma once

#include <delivery/core/CityModel.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/Types.hpp>
#include <delivery/infra/Config.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace delivery::core {

// Arista saliente del grafo.
//
// Un segmento bidireccional se representa con DOS Edge: uno en la lista de
// vecinos de `from` y otro en la de `to`. Un segmento oneWay tiene UNA sola
// Edge, en la lista de vecinos de `from`.
struct Edge {
    StreetId street;
    NodeId to;
    double lengthMeters = 0.0;
};

// ---------------------------------------------------------------------------
// StreetNetwork: grafo de calles inmutable tras su construccion.
//
// Modelo de uso:
//   auto net = StreetNetwork::build(cfg);
//   // net es shared_ptr<const StreetNetwork>. Inmutable. Compartible entre
//   // hilos sin sincronizacion.
//
// Fila 6 del mapa de conceptos: propiedad compartida con shared_ptr<const>.
// Fila 10: al ser inmutable, ningun metodo muta estado. No puede haber
// data races sobre el grafo.
//
// Internamente trabaja con indices (std::size_t) en lugar de NodeId para
// acelerar Dijkstra. Solo convierte al inicio y al final de las consultas.
// ---------------------------------------------------------------------------
class StreetNetwork {
public:
    // Construye la red desde la configuracion. Lanza ConfigReferenceError
    // si una calle referencia un nodo inexistente (aunque ConfigLoader ya
    // lo valida, verificamos por robustez).
    static std::shared_ptr<const StreetNetwork> build(const infra::Config& cfg);

    StreetNetwork(const StreetNetwork&) = delete;
    StreetNetwork& operator=(const StreetNetwork&) = delete;

    // Numero total de nodos.
    std::size_t nodeCount() const noexcept;

    // Consulta: vecinos salientes del nodo `u`.
    // Devuelve referencia a un vector inmutable. Vacio si el nodo no existe.
    // Precaucion: no guardar la referencia mas alla del tiempo de vida del
    // StreetNetwork.
    const std::vector<Edge>& neighbors(const NodeId& u) const;

    // Coordenadas de un nodo. nullopt si no existe.
    std::optional<LatLon> nodeLatLon(const NodeId& u) const;

    // Indice interno de un nodo. nullopt si no existe.
    std::optional<std::size_t> nodeIndex(const NodeId& u) const;

    // NodeId a partir del indice interno. nullopt si fuera de rango.
    std::optional<NodeId> nodeIdAt(std::size_t idx) const;

    // Longitud de la arista from -> to. nullopt si no existe.
    std::optional<double> edgeLength(const NodeId& from, const NodeId& to) const;

    // Numero total de aristas dirigidas (bidireccional cuenta como 2).
    std::size_t edgeCount() const noexcept;

private:
    StreetNetwork() = default;

    std::unordered_map<NodeId, std::size_t, NodeIdHash> nodeIndex_;
    std::vector<NodeId> nodeIds_;       // idx -> NodeId
    std::vector<LatLon> nodeCoords_;    // idx -> LatLon
    std::vector<std::vector<Edge>> adjacency_;  // idx -> vecinos salientes
    std::size_t edgeCount_ = 0;
};

}  // namespace delivery::core