#include <delivery/core/StreetNetwork.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace delivery::core {

std::shared_ptr<const StreetNetwork> StreetNetwork::build(const infra::Config& cfg) {
    // shared_ptr a un objeto no-const durante construccion, luego se
    // convierte a shared_ptr<const> al retornar.
    auto net = std::shared_ptr<StreetNetwork>(new StreetNetwork());

    // 1. Indexar nodos.
    net->nodeIds_.reserve(cfg.nodes.size());
    net->nodeCoords_.reserve(cfg.nodes.size());
    net->adjacency_.resize(cfg.nodes.size());

    for (std::size_t i = 0; i < cfg.nodes.size(); ++i) {
        const Node& n = cfg.nodes[i];
        net->nodeIndex_.emplace(n.id, i);
        net->nodeIds_.push_back(n.id);
        net->nodeCoords_.push_back(LatLon{n.lat, n.lon});
    }

    // 2. Construir adyacencias.
    for (const Street& s : cfg.streets) {
        const auto fromIt = net->nodeIndex_.find(s.from);
        const auto toIt = net->nodeIndex_.find(s.to);
        if (fromIt == net->nodeIndex_.end() || toIt == net->nodeIndex_.end()) {
            // ConfigLoader ya valida esto; si llega aqui, es un bug del loader.
            throw std::runtime_error(
                "StreetNetwork::build: street '" + s.id.str() +
                "' references unknown node");
        }

        const std::size_t iFrom = fromIt->second;
        const std::size_t iTo = toIt->second;

        const LatLon a = net->nodeCoords_[iFrom];
        const LatLon b = net->nodeCoords_[iTo];
        const double len = GeoBounds::distanceMeters(a, b);

        // Arista from -> to (siempre).
        net->adjacency_[iFrom].push_back(Edge{s.id, s.to, len});
        ++net->edgeCount_;

        // Arista to -> from (solo si no es oneWay).
        if (!s.oneWay) {
            net->adjacency_[iTo].push_back(Edge{s.id, s.from, len});
            ++net->edgeCount_;
        }
    }

    return net;
}

std::size_t StreetNetwork::nodeCount() const noexcept {
    return nodeIds_.size();
}

std::size_t StreetNetwork::edgeCount() const noexcept {
    return edgeCount_;
}

const std::vector<Edge>& StreetNetwork::neighbors(const NodeId& u) const {
    static const std::vector<Edge> empty;
    const auto it = nodeIndex_.find(u);
    if (it == nodeIndex_.end()) {
        return empty;
    }
    return adjacency_[it->second];
}

std::optional<LatLon> StreetNetwork::nodeLatLon(const NodeId& u) const {
    const auto it = nodeIndex_.find(u);
    if (it == nodeIndex_.end()) {
        return std::nullopt;
    }
    return nodeCoords_[it->second];
}

std::optional<std::size_t> StreetNetwork::nodeIndex(const NodeId& u) const {
    const auto it = nodeIndex_.find(u);
    if (it == nodeIndex_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<NodeId> StreetNetwork::nodeIdAt(std::size_t idx) const {
    if (idx >= nodeIds_.size()) {
        return std::nullopt;
    }
    return nodeIds_[idx];
}

std::optional<double> StreetNetwork::edgeLength(const NodeId& from,
                                                const NodeId& to) const {
    const auto it = nodeIndex_.find(from);
    if (it == nodeIndex_.end()) {
        return std::nullopt;
    }
    for (const Edge& e : adjacency_[it->second]) {
        if (e.to == to) {
            return e.lengthMeters;
        }
    }
    return std::nullopt;
}

}  // namespace delivery::core