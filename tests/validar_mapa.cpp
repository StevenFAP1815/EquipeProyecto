#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <utility>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void validarConexionesDuplicadas(const json& j) {
    std::set<std::pair<std::string, std::string>> conexionesUnicas;
    bool hayDuplicados = false;

    for (const auto& street : j["streets"]) {
        std::string id = street["id"];
        std::string from = street["from"];
        std::string to = street["to"];

        std::pair<std::string, std::string> conexion = {from, to};

        if (conexionesUnicas.count(conexion) > 0) {
            std::cout << "[ALERTA] Tramo duplicado encontrado: "
                      << "ID: " << id << " (" << from << " -> " << to << ")\n";
            hayDuplicados = true;
        } else {
            conexionesUnicas.insert(conexion);
        }
    }

    if (!hayDuplicados) {
        std::cout << "[OK] No se encontraron conexiones duplicadas en la red.\n";
    }
}

int main() {
    std::ifstream archivo("config/equipetrol.json");
    if (!archivo.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir el archivo config/equipetrol.json\n";
        return 1;
    }

    try {
        json j;
        archivo >> j;
        std::cout << "Mapa cargado correctamente. Analizando tramos...\n";
        validarConexionesDuplicadas(j);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR JSON] " << e.what() << "\n";
        return 1;
    }

    return 0;
}