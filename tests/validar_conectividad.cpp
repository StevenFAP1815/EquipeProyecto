#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void verificarConectividad(const json& j) {
    std::unordered_map<std::string, int> gradoEntrada;
    std::unordered_map<std::string, int> gradoSalida;
    std::unordered_set<std::string> todosLosNodos;

    // Registramos todos los nodos existentes
    for (const auto& node : j["nodes"]) {
        std::string id = node["id"];
        todosLosNodos.insert(id);
        gradoEntrada[id] = 0;
        gradoSalida[id] = 0;
    }

    // Contamos las entradas y salidas de cada nodo
    for (const auto& street : j["streets"]) {
        std::string from = street["from"];
        std::string to = street["to"];
        bool oneWay = street.value("oneWay", true);

        gradoSalida[from]++;
        gradoEntrada[to]++;

        if (!oneWay) {
            gradoSalida[to]++;
            gradoEntrada[from]++;
        }
    }

    int huérfanos = 0;
    int fuentes = 0;
    int sumideros = 0;

    std::cout << "   REPORTE DE CONECTIVIDAD Y NODOS ANÓMALOS   \n";

    for (const auto& id : todosLosNodos) {
        int in = gradoEntrada[id];
        int out = gradoSalida[id];

        if (in == 0 && out == 0) {
            std::cout << "[HUÉRFANO] Nodo " << id << " -> Desconectado (0 entradas, 0 salidas)\n";
            huérfanos++;
        } else if (in == 0) {
            std::cout << "[FUENTE]   Nodo " << id << " -> Solo salida (" << out << " salidas, 0 entradas)\n";
            fuentes++;
        } else if (out == 0) {
            std::cout << "[SUMIDERO] Nodo " << id << " -> Callejón sin salida (" << in << " entradas, 0 salidas)\n";
            sumideros++;
        }
    }

    std::cout << "\n--------------------------------------------\n";
    std::cout << "RESUMEN:\n";
    std::cout << " - Nodos huérfanos: " << huérfanos << "\n";
    std::cout << " - Nodos fuente:    " << fuentes << "\n";
    std::cout << " - Nodos sumidero:  " << sumideros << "\n";
    std::cout << "--------------------------------------------\n";

    if (huérfanos == 0 && fuentes == 0 && sumideros == 0) {
        std::cout << "[OK] Red plenamente conectada. Sin nodos aislados ni trampa.\n";
    }
}

int main() {
    std::ifstream archivo("config/equipetrol.json");
    if (!archivo.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir config/equipetrol.json\n";
        return 1;
    }

    try {
        json j;
        archivo >> j;
        verificarConectividad(j);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR PARSING JSON] " << e.what() << "\n";
        return 1;
    }

    return 0;
}