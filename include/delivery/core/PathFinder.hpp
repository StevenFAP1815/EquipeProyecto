#pragma once

#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/Types.hpp>

#include <optional>
#include <vector>

namespace delivery::core {
struct RouteResult {
    std::vector<NodeId> path;
    double totalMeters = 0.0;
};

std::optional<RouteResult> findShortestPath(const StreetNetwork& network,
                                            const NodeId& from,
                                            const NodeId& to);
}
