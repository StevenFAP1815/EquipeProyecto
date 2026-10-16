#include <delivery/infra/ConfigLoader.hpp>

#include <nlohmann/json.hpp>

#include <string_view>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace delivery::infra {

namespace {

using nlohmann::json;
using namespace delivery::core;


[[noreturn]] void schemaError(const std::string& what) {
    throw ConfigSchemaError(what);
}

const json& requireMember(const json& obj, std::string_view key,
                        std::string_view context) {
    if (!obj.is_object()) {
        schemaError("expected JSON object at " + std::string(context));
    }
    // nlohmann::json::find acepta std::string_view en v3.11.
    const auto it = obj.find(key);
    if (it == obj.end()) {
        schemaError("missing required property '" + std::string(key) +
                    "' at " + std::string(context));
    }
    return *it;
}

std::string readString(const json& obj, std::string_view key,std::string_view context){
    const json& v = requireMember(obj, key, context);
    if (!v.is_string()) {
        schemaError("property '" + std::string(key) + "' at " +
                std::string(context) + " must be a string");
    }
return v.get<std::string>();
}

long long readIntRaw(const json& v, const std::string& key,
                    const std::string& context,
                    long long minValue, long long maxValue){

    if (!v.is_number_integer() && !v.is_number_unsigned()) {
        schemaError("property '" + key + "' at " + context +
                    " must be an integer");
    }
    const long long raw = v.get<long long>();
    if (raw < minValue || raw > maxValue) {
        std::ostringstream oss;
        oss << "property '" << key << "' at " << context
            << " out of range [" << minValue << ", " << maxValue
            << "]: got " << raw;
        schemaError(oss.str());
    }
    return raw;
}

int readInt(const json& obj, const std::string& key,
            const std::string& context,
            long long minValue = std::numeric_limits<long long>::min(),
            long long maxValue = std::numeric_limits<long long>::max()) {
    const json& v = requireMember(obj, key, context);
    const long long raw = readIntRaw(v, key, context, minValue, maxValue);
    return static_cast<int>(raw);
}

// Sobrecarga para arrays: index debe ser size_t para no colisionar con string.
int readIntArrayElement(const json& arr, std::size_t index,
                        const std::string& context,
                        long long minValue = std::numeric_limits<long long>::min(),
                        long long maxValue = std::numeric_limits<long long>::max()) {
    if (!arr.is_array() || index >= arr.size()) {
        schemaError("index out of bounds at " + context);
    }
    const long long raw = readIntRaw(arr[index],
                                     std::to_string(index), context,
                                     minValue, maxValue);
    return static_cast<int>(raw);
}

double readDouble(const json& obj, const std::string& key,
                  const std::string& context) {
    const json& v = requireMember(obj, key, context);
    if (!v.is_number()) {
        schemaError("property '" + key + "' at " + context +
                    " must be a number");
    }
    return v.get<double>();
}

bool readBool(const json& obj, const std::string& key,
              const std::string& context) {
    const json& v = requireMember(obj, key, context);
    if (!v.is_boolean()) {
        schemaError("property '" + key + "' at " + context +
                    " must be a boolean");
    }
    return v.get<bool>();
}

// ---------------------------------------------------------------------------

MapConfig parseMap(const json& j) {
    MapConfig m;
    m.imagePath   = readString(j, "image",       "map");
    m.attribution = readString(j, "attribution", "map");

    const json& boundsJson = requireMember(j, "bounds", "map");
    const double north = readDouble(boundsJson, "north", "map.bounds");
    const double south = readDouble(boundsJson, "south", "map.bounds");
    const double west  = readDouble(boundsJson, "west",  "map.bounds");
    const double east  = readDouble(boundsJson, "east",  "map.bounds");

    m.bounds = GeoBounds(north, south, west, east);
    try {
        m.bounds.validate();
    } catch (const std::invalid_argument& e) {
        schemaError(std::string("map.bounds invalid: ") + e.what());
    }
    return m;
}

std::vector<Node> parseNodes(const json& j) {
    if (!j.is_array()) {
        schemaError("'nodes' must be an array");
    }
    if (j.empty()) {
        schemaError("'nodes' must contain at least one element");
    }
    std::vector<Node> nodes;
    nodes.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i) {
        const json& item = j[i];
        const std::string ctx = "nodes[" + std::to_string(i) + "]";
        Node n;
        n.id  = NodeId(readString(item, "id", ctx));
        n.lat = readDouble(item, "lat", ctx);
        n.lon = readDouble(item, "lon", ctx);
        if (n.id.empty()) {
            schemaError("empty node id at " + ctx);
        }
        nodes.push_back(std::move(n));
    }
    return nodes;
}

std::vector<Street> parseStreets(const json& j) {
    if (!j.is_array()) {
        schemaError("'streets' must be an array");
    }
    std::vector<Street> streets;
    streets.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i) {
        const json& item = j[i];
        const std::string ctx = "streets[" + std::to_string(i) + "]";
        Street s;
        s.id     = StreetId(readString(item, "id", ctx));
        s.from   = NodeId(readString(item, "from", ctx));
        s.to     = NodeId(readString(item, "to", ctx));
        s.oneWay = readBool(item, "oneWay", ctx);
        if (s.id.empty()) {
            schemaError("empty street id at " + ctx);
        }
        streets.push_back(std::move(s));
    }
    return streets;
}

std::vector<Restaurant> parseRestaurants(const json& j) {
    if (!j.is_array()) {
        schemaError("'restaurants' must be an array");
    }
    std::vector<Restaurant> restaurants;
    restaurants.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i) {
        const json& item = j[i];
        const std::string ctx = "restaurants[" + std::to_string(i) + "]";
        Restaurant r;
        r.id          = RestaurantId(readString(item, "id", ctx));
        r.name        = readString(item, "name", ctx);
        r.node        = NodeId(readString(item, "node", ctx));
        r.pickupSlots = readInt(item, "pickupSlots", ctx, 1);
        if (r.id.empty()) {
            schemaError("empty restaurant id at " + ctx);
        }

        const json& prepJson = requireMember(item, "prepTimeMs", ctx);
        if (!prepJson.is_array() || prepJson.size() != 2) {
            schemaError("prepTimeMs at " + ctx +
                        " must be an array of exactly two integers [min, max]");
        }
        const int minMs = readIntArrayElement(prepJson, 0, ctx + ".prepTimeMs", 0);
        const int maxMs = readIntArrayElement(prepJson, 1, ctx + ".prepTimeMs", 0);
        if (minMs > maxMs) {
            schemaError("prepTimeMs at " + ctx +
                        " has min > max (" + std::to_string(minMs) +
                        " > " + std::to_string(maxMs) + ")");
        }
        r.prepTimeMs = TimeRange{minMs, maxMs};

        restaurants.push_back(std::move(r));
    }
    return restaurants;
}

FleetConfig parseFleet(const json& j) {
    FleetConfig f;
    f.couriers    = readInt(j, "couriers",    "fleet", 1);
    f.bagCapacity = readInt(j, "bagCapacity", "fleet", 1);
    f.speedKmh    = readDouble(j, "speedKmh", "fleet");
    if (f.speedKmh <= 0.0) {
        schemaError("fleet.speedKmh must be positive");
    }
    f.startNode = NodeId(readString(j, "startNode", "fleet"));
    if (f.startNode.empty()) {
        schemaError("fleet.startNode must not be empty");
    }
    return f;
}

OrdersConfig parseOrders(const json& j) {
    OrdersConfig o;
    o.meanIntervalMs = readInt(j, "meanIntervalMs", "orders", 1);
    o.burstMax       = readInt(j, "burstMax",       "orders", 1);
    o.maxPending     = readInt(j, "maxPending",     "orders", 0);

    const json& seedJson = requireMember(j, "seed", "orders");
    if (!seedJson.is_number_unsigned() && !seedJson.is_number_integer()) {
        schemaError("orders.seed must be an integer");
    }
    o.seed = seedJson.get<std::uint64_t>();
    return o;
}

DispatchConfig parseDispatch(const json& j) {
    DispatchConfig d;
    d.quoteTimeoutMs  = readInt(j, "quoteTimeoutMs",  "dispatch", 1);
    d.acceptTimeoutMs = readInt(j, "acceptTimeoutMs", "dispatch", 0);
    return d;
}

IncidentsConfig parseIncidents(const json& j) {
    IncidentsConfig i;
    i.breakdownProbability = readDouble(j, "breakdownProbability", "incidents");
    if (i.breakdownProbability < 0.0 || i.breakdownProbability > 1.0) {
        schemaError("incidents.breakdownProbability must be in [0, 1]");
    }
    return i;
}

SimulationConfig parseSimulation(const json& j) {
    SimulationConfig s;
    s.durationS = readInt(j, "durationS", "simulation", 0);
    s.timeScale = readDouble(j, "timeScale", "simulation");
    if (s.timeScale <= 0.0) {
        schemaError("simulation.timeScale must be positive");
    }
    return s;
}

// ---------------------------------------------------------------------------

void buildNodeIndex(Config& cfg) {
    for (std::size_t i = 0; i < cfg.nodes.size(); ++i) {
        const auto [it, inserted] = cfg.nodeIndex.emplace(cfg.nodes[i].id, i);
        if (!inserted) {
            throw ConfigReferenceError(
                "duplicate node id '" + cfg.nodes[i].id.str() + "'");
        }
    }
}

void buildStreetIndex(Config& cfg) {
    for (std::size_t i = 0; i < cfg.streets.size(); ++i) {
        const auto [it, inserted] = cfg.streetIndex.emplace(cfg.streets[i].id, i);
        if (!inserted) {
            throw ConfigReferenceError(
                "duplicate street id '" + cfg.streets[i].id.str() + "'");
        }
    }
}

void buildRestaurantIndex(Config& cfg) {
    for (std::size_t i = 0; i < cfg.restaurants.size(); ++i) {
        const auto [it, inserted] =
            cfg.restaurantIndex.emplace(cfg.restaurants[i].id, i);
        if (!inserted) {
            throw ConfigReferenceError(
                "duplicate restaurant id '" + cfg.restaurants[i].id.str() + "'");
        }
    }
}

void validateReferences(const Config& cfg) {
    for (const Street& s : cfg.streets) {
        if (cfg.nodeIndex.find(s.from) == cfg.nodeIndex.end()) {
            throw ConfigReferenceError(
                "street '" + s.id.str() + "' references unknown node '" +
                s.from.str() + "' (from)");
        }
        if (cfg.nodeIndex.find(s.to) == cfg.nodeIndex.end()) {
            throw ConfigReferenceError(
                "street '" + s.id.str() + "' references unknown node '" +
                s.to.str() + "' (to)");
        }
    }
    for (const Restaurant& r : cfg.restaurants) {
        if (cfg.nodeIndex.find(r.node) == cfg.nodeIndex.end()) {
            throw ConfigReferenceError(
                "restaurant '" + r.id.str() + "' references unknown node '" +
                r.node.str() + "'");
        }
    }
}

}  // namespace

Config parseConfig(const nlohmann::json& j) {
    if (!j.is_object()) {
        throw ConfigJsonError("top-level JSON value must be an object");
    }

    Config cfg;
    cfg.map         = parseMap(requireMember(j, "map", "root"));
    cfg.nodes       = parseNodes(requireMember(j, "nodes", "root"));
    cfg.streets     = parseStreets(requireMember(j, "streets", "root"));
    cfg.restaurants = parseRestaurants(requireMember(j, "restaurants", "root"));
    cfg.fleet       = parseFleet(requireMember(j, "fleet", "root"));
    cfg.orders      = parseOrders(requireMember(j, "orders", "root"));
    cfg.dispatch    = parseDispatch(requireMember(j, "dispatch", "root"));
    cfg.incidents   = parseIncidents(requireMember(j, "incidents", "root"));
    cfg.simulation  = parseSimulation(requireMember(j, "simulation", "root"));

    buildNodeIndex(cfg);
    buildStreetIndex(cfg);
    buildRestaurantIndex(cfg);
    validateReferences(cfg);

    return cfg;
}

Config loadConfigFromFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        throw ConfigFileError("config file not found: " + path.string());
    }
    if (!std::filesystem::is_regular_file(path, ec)) {
        throw ConfigFileError("config path is not a regular file: " +
                              path.string());
    }

    std::ifstream in(path);
    if (!in) {
        throw ConfigFileError("cannot open config file: " + path.string());
    }

    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        throw ConfigJsonError(std::string("JSON parse error in ") +
                              path.string() + ": " + e.what());
    } catch (const json::exception& e) {
        throw ConfigJsonError(std::string("JSON error in ") + path.string() +
                              ": " + e.what());
    }

    return parseConfig(j);
}

}  // namespace delivery::infra