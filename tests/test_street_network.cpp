#include <catch2/catch_test_macros.hpp>

#include <delivery/core/PathFinder.hpp>
#include <delivery/core/StreetNetwork.hpp>

#include <stdexcept>
#include <vector>

using namespace delivery::core;

namespace {
std::vector<Node> makeNodes() {
    return {
        Node{NodeId("n0"), -17.7600, -63.2000},
        Node{NodeId("n1"), -17.7600, -63.1900},
        Node{NodeId("n2"), -17.7600, -63.1800},
        Node{NodeId("n3"), -17.7000, -63.1900},
        Node{NodeId("n4"), -17.7700, -63.1800},
        Node{NodeId("n5"), -17.8000, -63.2500},
    };
}

std::vector<Street> makeStreets() {
    return {
        Street{StreetId("s0"), NodeId("n0"), NodeId("n1"), false},
        Street{StreetId("s1"), NodeId("n1"), NodeId("n2"), false},
        Street{StreetId("s2"), NodeId("n0"), NodeId("n3"), false},
        Street{StreetId("s3"), NodeId("n3"), NodeId("n2"), false},
        Street{StreetId("s4"), NodeId("n2"), NodeId("n4"), true},
    };
}

std::vector<NodeId> ids(std::initializer_list<const char*> names) {
    std::vector<NodeId> out;
    for (const char* n : names) {
        out.emplace_back(n);
    }
    return out;
}
}

TEST_CASE("StreetNetwork: una calle doble cuenta dos aristas y una de un sentido cuenta una",
          "[street_network]") {
    const auto net = StreetNetwork::build(makeNodes(), makeStreets());

    REQUIRE(net->nodeCount() == 6);
    REQUIRE(net->edgeCount() == 4 * 2 + 1);
}

TEST_CASE("StreetNetwork: respeta el sentido de las calles de un solo sentido",
          "[street_network]") {
    const auto net = StreetNetwork::build(makeNodes(), makeStreets());

    REQUIRE(net->edgeLength(NodeId("n2"), NodeId("n4")).has_value());
    REQUIRE_FALSE(net->edgeLength(NodeId("n4"), NodeId("n2")).has_value());
    REQUIRE(net->neighbors(NodeId("n4")).empty());
}

TEST_CASE("StreetNetwork: una calle hacia un nodo inexistente lanza error",
          "[street_network]") {
    std::vector<Street> streets = makeStreets();
    streets.push_back(Street{StreetId("s9"), NodeId("n0"), NodeId("n99"), false});

    REQUIRE_THROWS_AS(StreetNetwork::build(makeNodes(), streets), std::runtime_error);
}

TEST_CASE("PathFinder: elige el camino mas corto", "[pathfinder]") {
    const auto net = StreetNetwork::build(makeNodes(), makeStreets());

    const auto route = findShortestPath(*net, NodeId("n0"), NodeId("n2"));

    REQUIRE(route.has_value());
    REQUIRE(route->path == ids({"n0", "n1", "n2"}));
    const double expected = *net->edgeLength(NodeId("n0"), NodeId("n1")) +
                            *net->edgeLength(NodeId("n1"), NodeId("n2"));
    REQUIRE(route->totalMeters == expected);
}

TEST_CASE("PathFinder: no circula en contra de una calle de un solo sentido",
          "[pathfinder]") {
    const auto net = StreetNetwork::build(makeNodes(), makeStreets());

    REQUIRE(findShortestPath(*net, NodeId("n0"), NodeId("n4")).has_value());
    REQUIRE_FALSE(findShortestPath(*net, NodeId("n4"), NodeId("n0")).has_value());
}

TEST_CASE("PathFinder: casos limite", "[pathfinder]") {
    const auto net = StreetNetwork::build(makeNodes(), makeStreets());

    const auto same = findShortestPath(*net, NodeId("n1"), NodeId("n1"));
    REQUIRE(same.has_value());
    REQUIRE(same->path == ids({"n1"}));
    REQUIRE(same->totalMeters == 0.0);

    REQUIRE_FALSE(findShortestPath(*net, NodeId("n0"), NodeId("n5")).has_value());
    REQUIRE_FALSE(findShortestPath(*net, NodeId("n0"), NodeId("n99")).has_value());
}
