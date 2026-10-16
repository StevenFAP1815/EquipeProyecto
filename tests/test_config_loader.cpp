#include <catch2/catch_test_macros.hpp>

#include <delivery/infra/ConfigLoader.hpp>

#include <filesystem>
#include <string>

using namespace delivery::infra;

namespace {

// Ruta a tests/data/ relativa al directorio de build.
// Catch2 se ejecuta desde el directorio build/ por defecto con ctest.
std::filesystem::path dataPath(const std::string& filename) {
    return std::filesystem::path(TESTS_DATA_DIR) / filename;
}

}  // namespace

// ---------------------------------------------------------------------------
// Casos validos
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader: carga equipetrol.json correctamente",
          "[config_loader][valid]") {
    const Config cfg = loadConfigFromFile(dataPath("equipetrol.json"));

    REQUIRE(cfg.map.imagePath == "data/equipetrol.png");
    REQUIRE(cfg.map.attribution == "(c) OpenStreetMap contributors");
    REQUIRE(cfg.nodes.size() == 5);
    REQUIRE(cfg.streets.size() == 6);
    REQUIRE(cfg.restaurants.size() == 2);
    REQUIRE(cfg.fleet.couriers == 3);
    REQUIRE(cfg.fleet.bagCapacity == 2);
    REQUIRE(cfg.fleet.startNode == delivery::core::NodeId("n0"));
    REQUIRE(cfg.orders.seed == 42);
}

TEST_CASE("ConfigLoader: indices auxiliares apuntan a posiciones correctas",
          "[config_loader][valid]") {
    const Config cfg = loadConfigFromFile(dataPath("equipetrol.json"));

    const auto it = cfg.nodeIndex.find(delivery::core::NodeId("n2"));
    REQUIRE(it != cfg.nodeIndex.end());
    REQUIRE(cfg.nodes[it->second].id == delivery::core::NodeId("n2"));

    const auto itR = cfg.restaurantIndex.find(delivery::core::RestaurantId("r1"));
    REQUIRE(itR != cfg.restaurantIndex.end());
    REQUIRE(cfg.restaurants[itR->second].name == "Example R1");
}

// ---------------------------------------------------------------------------
// Casos invalidos
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader: archivo inexistente lanza ConfigFileError",
          "[config_loader][invalid]") {
    REQUIRE_THROWS_AS(
        loadConfigFromFile(dataPath("no_existe_este_archivo.json")),
        ConfigFileError);
}

TEST_CASE("ConfigLoader: JSON mal formado lanza ConfigJsonError",
          "[config_loader][invalid]") {
    REQUIRE_THROWS_AS(
        loadConfigFromFile(dataPath("invalid_bad_json.json")),
        ConfigJsonError);
}

TEST_CASE("ConfigLoader: falta 'map' lanza ConfigSchemaError",
          "[config_loader][invalid]") {
    REQUIRE_THROWS_AS(
        loadConfigFromFile(dataPath("invalid_missing_map.json")),
        ConfigSchemaError);
}

TEST_CASE("ConfigLoader: calle huerfana lanza ConfigReferenceError",
          "[config_loader][invalid]") {
    REQUIRE_THROWS_AS(
        loadConfigFromFile(dataPath("invalid_orphan_street.json")),
        ConfigReferenceError);
}

TEST_CASE("ConfigLoader: nodo duplicado lanza ConfigReferenceError",
          "[config_loader][invalid]") {
    REQUIRE_THROWS_AS(
        loadConfigFromFile(dataPath("invalid_duplicate_node.json")),
        ConfigReferenceError);
}

// ---------------------------------------------------------------------------
// Verificacion de mensajes (los tests no dependen del texto exacto, pero
// verifican que los mensajes contienen la informacion clave).
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader: mensaje de huerfano menciona la calle y el nodo",
          "[config_loader][invalid][message]") {
    try {
        loadConfigFromFile(dataPath("invalid_orphan_street.json"));
        FAIL("Se esperaba ConfigReferenceError");
    } catch (const ConfigReferenceError& e) {
        const std::string msg = e.what();
        REQUIRE(msg.find("s0") != std::string::npos);
        REQUIRE(msg.find("n99") != std::string::npos);
    }
}

TEST_CASE("ConfigLoader: mensaje de duplicado menciona el id",
          "[config_loader][invalid][message]") {
    try {
        loadConfigFromFile(dataPath("invalid_duplicate_node.json"));
        FAIL("Se esperaba ConfigReferenceError");
    } catch (const ConfigReferenceError& e) {
        const std::string msg = e.what();
        REQUIRE(msg.find("n0") != std::string::npos);
    }
}