#pragma once

#include "MapData.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace delivery {

class MapLoader {
public:
    // Carga los datos del JSON y calcula las posiciones en pixeles de cada nodo
    static bool loadFromFile(
        const std::string& jsonPath,
        MapMetadata& outMetadata,
        std::unordered_map<std::string, Node>& outNodes,
        std::vector<Street>& outStreets,
        std::vector<Restaurant>& outRestaurants,
        int imageWidth,
        int imageHeight) 
    {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            std::cerr << "[MapLoader Error] No se pudo abrir el archivo: " << jsonPath << std::endl;
            return false;
        }

        nlohmann::json j;
        try {
            file >> j;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[MapLoader Error] Error al parsear JSON: " << e.what() << std::endl;
            return false;
        }

        // 1. Cargar Metadata y Bounds
        if (j.contains("map")) {
            auto mapJson = j["map"];
            outMetadata.imagePath = mapJson.value("image", "");
            outMetadata.attribution = mapJson.value("attribution", "");
            
            if (mapJson.contains("bounds")) {
                auto b = mapJson["bounds"];
                outMetadata.bounds.north = b.value("north", 0.0);
                outMetadata.bounds.south = b.value("south", 0.0);
                outMetadata.bounds.west  = b.value("west", 0.0);
                outMetadata.bounds.east  = b.value("east", 0.0);
            }
        }

        // 2. Cargar Nodos y proyectar a Pixeles
        outNodes.clear();
        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& item : j["nodes"]) {
                Node node;
                node.id = item.value("id", "");
                node.lat = item.value("lat", 0.0);
                node.lon = item.value("lon", 0.0);

                // Conversion a pixeles de la imagen
                auto [px, py] = CoordinateConverter::latLonToPixel(
                    node.lat, node.lon, 
                    outMetadata.bounds, 
                    imageWidth, imageHeight
                );
                
                node.pixelX = px;
                node.pixelY = py;

                outNodes[node.id] = node;
            }
        }

        // 3. Cargar Calles
        outStreets.clear();
        if (j.contains("streets") && j["streets"].is_array()) {
            for (const auto& item : j["streets"]) {
                Street street;
                street.id = item.value("id", "");
                street.fromNodeId = item.value("from", "");
                street.toNodeId = item.value("to", "");
                street.oneWay = item.value("oneWay", false);

                outStreets.push_back(street);

                // Poblar adyacencias en los nodos si existen
                if (outNodes.count(street.fromNodeId)) {
                    outNodes[street.fromNodeId].neighborNodeIds.push_back(street.toNodeId);
                }
                if (!street.oneWay && outNodes.count(street.toNodeId)) {
                    outNodes[street.toNodeId].neighborNodeIds.push_back(street.fromNodeId);
                }
            }
        }

        // 4. Cargar Restaurantes
        outRestaurants.clear();
        if (j.contains("restaurants") && j["restaurants"].is_array()) {
            for (const auto& item : j["restaurants"]) {
                Restaurant r;
                r.id = item.value("id", "");
                r.name = item.value("name", "");
                r.nodeId = item.value("node", "");
                r.pickupSlots = item.value("pickupSlots", 1);
                
                if (item.contains("prepTimeMs") && item["prepTimeMs"].is_array() && item["prepTimeMs"].size() >= 2) {
                    r.prepTimeMs.first = item["prepTimeMs"][0];
                    r.prepTimeMs.second = item["prepTimeMs"][1];
                }

                outRestaurants.push_back(r);
            }
        }

        return true;
    }
};

} // namespace delivery