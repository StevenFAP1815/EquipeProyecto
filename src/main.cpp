#include <delivery/infra/ConfigLoader.hpp>

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace delivery::infra;

    if (argc < 2) {
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>]\n";
        return 1;
    }

    try {
        const Config cfg = loadConfigFromFile(argv[1]);

        std::cout << "Config loaded successfully.\n";
        std::cout << "  map.image       : " << cfg.map.imagePath << "\n";
        std::cout << "  map.attribution : " << cfg.map.attribution << "\n";
        std::cout << "  map.bounds      : N=" << cfg.map.bounds.north()
                  << " S=" << cfg.map.bounds.south()
                  << " W=" << cfg.map.bounds.west()
                  << " E=" << cfg.map.bounds.east() << "\n";
        std::cout << "  nodes           : " << cfg.nodes.size() << "\n";
        std::cout << "  streets         : " << cfg.streets.size() << "\n";
        std::cout << "  restaurants     : " << cfg.restaurants.size() << "\n";
        std::cout << "  fleet.couriers  : " << cfg.fleet.couriers << "\n";
        std::cout << "  fleet.startNode : " << cfg.fleet.startNode << "\n";
        std::cout << "  orders.seed     : " << cfg.orders.seed << "\n";
        std::cout << "  simulation.timeScale : " << cfg.simulation.timeScale << "\n";
        return 0;
    } catch (const ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 2;
    }
}