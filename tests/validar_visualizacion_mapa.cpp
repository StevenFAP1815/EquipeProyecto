#include <iostream>
#include <opencv2/opencv.hpp>
#include "MapData.hpp"
#include "MapLoader.hpp"

int main(int argc, char** argv) {
    std::string jsonPath = "../config/equipetrol.json";
    if (argc > 1) {
        jsonPath = argv[1];
    }

    std::cout << "[Visualizacion] Cargando configuracion desde: " << jsonPath << std::endl;

    cv::Mat mapImage = cv::imread("C:/Users/cjant/Desktop/EquipeProyecto/data/mapa_equipetrol.png");
    if (mapImage.empty()) {
        std::cerr << "[Error] No se pudo cargar la imagen en ../data/mapa_equipetrol.png" << std::endl;
        return -1;
    }

    int imgWidth = mapImage.cols;
    int imgHeight = mapImage.rows;

    std::cout << "[Visualizacion] Imagen cargada: " << imgWidth << "x" << imgHeight << " px" << std::endl;

    delivery::MapMetadata metadata;
    std::unordered_map<std::string, delivery::Node> nodes;
    std::vector<delivery::Street> streets;
    std::vector<delivery::Restaurant> restaurants;

    if (!delivery::MapLoader::loadFromFile(jsonPath, metadata, nodes, streets, restaurants, imgWidth, imgHeight)) {
        std::cerr << "[Error] Fallo al procesar el archivo JSON." << std::endl;
        return -1;
    }

    std::cout << "[Visualizacion] Carga exitosa: "
              << nodes.size() << " nodos, "
              << streets.size() << " calles, "
              << restaurants.size() << " restaurantes." << std::endl;

    for (const auto& street : streets) {
        if (nodes.count(street.fromNodeId) && nodes.count(street.toNodeId)) {
            const auto& n1 = nodes[street.fromNodeId];
            const auto& n2 = nodes[street.toNodeId];

            cv::Point pt1(n1.pixelX, n1.pixelY);
            cv::Point pt2(n2.pixelX, n2.pixelY);

            cv::line(mapImage, pt1, pt2, cv::Scalar(255, 0, 0), 2);
        }
    }

    for (const auto& [id, node] : nodes) {
        cv::Point pt(node.pixelX, node.pixelY);
        cv::circle(mapImage, pt, 4, cv::Scalar(0, 0, 255), -1);
    }

    for (const auto& rest : restaurants) {
        if (nodes.count(rest.nodeId)) {
            const auto& node = nodes[rest.nodeId];
            cv::Point pt(node.pixelX, node.pixelY);

            cv::circle(mapImage, pt, 7, cv::Scalar(0, 255, 0), -1);
            cv::putText(mapImage, rest.name, cv::Point(pt.x + 8, pt.y + 4),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
        }
    }

    cv::imshow("Validacion de Mapa - Equipetrol", mapImage);
    std::cout << "[Visualizacion] Presiona cualquier tecla en la ventana para salir..." << std::endl;
    cv::waitKey(0);

    return 0;
}
