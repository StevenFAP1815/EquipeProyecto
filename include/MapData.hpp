#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

namespace delivery {

// Limites geograficos de la imagen del mapa
struct MapBounds {
    double north{0.0};
    double south{0.0};
    double west{0.0};
    double east{0.0};
};

// Informacion general del mapa
struct MapMetadata {
    std::string imagePath;
    std::string attribution;
    MapBounds bounds;
};

// Nodo del grafo (Interseccion o punto de interes)
struct Node {
    std::string id;
    double lat{0.0};
    double lon{0.0};
    
    // Coordenadas calculadas en pixeles dentro de la imagen
    int pixelX{0};
    int pixelY{0};

    // Punteros/IDs a calles adyacentes (se poblaran al construir el grafo)
    std::vector<std::string> neighborNodeIds;
};

// Calle que conecta dos nodos
struct Street {
    std::string id;
    std::string fromNodeId;
    std::string toNodeId;
    bool oneWay{false};
};

// Restaurante en el mapa
struct Restaurant {
    std::string id;
    std::string name;
    std::string nodeId;
    int pickupSlots{1};
    std::pair<int, int> prepTimeMs{60000, 240000};
};

// Clase encargada de la conversion de coordenadas
class CoordinateConverter {
public:
    static std::pair<int, int> latLonToPixel(
        double lat, double lon, 
        const MapBounds& bounds, 
        int imageWidth, int imageHeight) 
    {
        // Normalización a rango [0.0, 1.0]
        double normX = (lon - bounds.west) / (bounds.east - bounds.west);
        // Invertimos el eje Y porque los pixeles crecen hacia abajo
        double normY = (bounds.north - lat) / (bounds.north - bounds.south);

        int px = static_cast<int>(normX * imageWidth);
        int py = static_cast<int>(normY * imageHeight);

        return {px, py};
    }
};

} // namespace delivery