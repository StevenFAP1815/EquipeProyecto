#include <iostream>
#include <fstream>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

constexpr double PI = 3.14159265358979323846;

struct Punto {
    double lat;
    double lon;
};

// Cálculo de distancia en metros entre dos coordenadas (Fórmula de Haversine)
double calcularDistanciaMetros(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0; // Radio de la Tierra en metros
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;

    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * PI / 180.0) * std::cos(lat2 * PI / 180.0) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}

void validarGeometria(const json& j) {
    // 1. Cargar Bounding Box declarado
    auto bounds = j["map"]["bounds"];
    double north = bounds["north"];
    double south = bounds["south"];
    double west  = bounds["west"];
    double east  = bounds["east"];

    std::unordered_map<std::string, Punto> nodos;
    
    double minLat = 90.0, maxLat = -90.0;
    double minLon = 180.0, maxLon = -180.0;
    int nodosFueraDeBounds = 0;

    // 2. Comprobar nodos y límites geográficos
    for (const auto& node : j["nodes"]) {
        std::string id = node["id"];
        double lat = node["lat"];
        double lon = node["lon"];

        nodos[id] = {lat, lon};

        // Extensión real encontrada
        minLat = std::min(minLat, lat);
        maxLat = std::max(maxLat, lat);
        minLon = std::min(minLon, lon);
        maxLon = std::max(maxLon, lon);

        // Verificar si está dentro de las fronteras declaradas
        if (lat > north || lat < south || lon < west || lon > east) {
            std::cout << "[ALERTA BOUNDS] Nodo " << id 
                      << " (" << lat << ", " << lon << ") está fuera del cuadro delimitador.\n";
            nodosFueraDeBounds++;
        }
    }

    std::cout << "      REPORTE DE GEOMETRÍA Y LIMITES        \n";

    std::cout << "Límites declarados en JSON:\n";
    std::cout << "  Norte: " << north << ", Sur: " << south 
              << ", Oeste: " << west << ", Este: " << east << "\n\n";

    std::cout << "Extensión real de los nodos:\n";
    std::cout << "  Norte: " << maxLat << ", Sur: " << minLat 
              << ", Oeste: " << minLon << ", Este: " << maxLon << "\n\n";

    if (nodosFueraDeBounds == 0) {
        std::cout << "[OK] Todos los nodos están dentro de los límites declarados.\n";
    } else {
        std::cout << "[PRECAUCIÓN] Hay " << nodosFueraDeBounds << " nodo(s) fuera del cuadro delimitador.\n";
    }

    // 3. Verificar trazado de calles (longitud de segmentos)
    std::cout << "\n--------------------------------------------\n";
    std::cout << "ANÁLISIS DE LONGITUD DE TRAMOS DE CALLE:\n";
    
    double maxDist = 0.0;
    std::string segmentoLargoId = "";
    int tramosSospechosos = 0;

    // En Equipetrol, las manzanas urbanas suelen medir entre 50m y 250m.
    // Un segmento > 400m suele indicar que atraviesa manzanas incorrectamente.
    const double UBRAL_LONGITUD_SOSPECHOSA = 400.0; 

    for (const auto& street : j["streets"]) {
        std::string id = street["id"];
        std::string u = street["from"];
        std::string v = street["to"];

        if (nodos.count(u) && nodos.count(v)) {
            double d = calcularDistanciaMetros(nodos[u].lat, nodos[u].lon, nodos[v].lat, nodos[v].lon);
            
            if (d > maxDist) {
                maxDist = d;
                segmentoLargoId = id;
            }

            if (d > UBRAL_LONGITUD_SOSPECHOSA) {
                std::cout << "[TRAMO LARGO / SOSPECHOSO] " << id << " (" << u << " -> " << v 
                          << "): " << d << " metros.\n";
                tramosSospechosos++;
            }
        }
    }

    std::cout << "\nSegmento más largo del mapa: " << segmentoLargoId << " (" << maxDist << " metros).\n";
    
    if (tramosSospechosos == 0) {
        std::cout << "[OK] Todos los tramos respetan el trazado regular de la grilla urbana.\n";
    } else {
        std::cout << "[AVISO] Revisar los tramos marcados para confirmar que no se salten intersecciones.\n";
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
        validarGeometria(j);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR PARSING JSON] " << e.what() << "\n";
        return 1;
    }

    return 0;
}