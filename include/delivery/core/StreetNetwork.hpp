#pragma once

#include <delivery/core/CityModel.hpp>
#include <delivery/core/GeoBounds.hpp>
#include <delivery/core/Types.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace delivery::core {
struct Edge {
    StreetId street;
    NodeId to;
    double lengthMeters = 0.0;
};

class StreetNetwork {
public:
    static std::shared_ptr<const StreetNetwork> build(const std::vector<Node>& nodes,
                                                      const std::vector<Street>& streets);

    StreetNetwork(const StreetNetwork&) = delete;
    StreetNetwork& operator=(const StreetNetwork&) = delete;

    std::size_t nodeCount() const noexcept;

    const std::vector<Edge>& neighbors(const NodeId& u) const;

    std::optional<LatLon> nodeLatLon(const NodeId& u) const;

    std::optional<std::size_t> nodeIndex(const NodeId& u) const;

    std::optional<NodeId> nodeIdAt(std::size_t idx) const;

    std::optional<double> edgeLength(const NodeId& from, const NodeId& to) const;

    std::size_t edgeCount() const noexcept;

private:
    StreetNetwork() = default;

    std::unordered_map<NodeId, std::size_t, NodeIdHash> nodeIndex_;
    std::vector<NodeId> nodeIds_;
    std::vector<LatLon> nodeCoords_;
    std::vector<std::vector<Edge>> adjacency_;
    std::size_t edgeCount_ = 0;
};
}
