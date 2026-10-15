#include <delivery/core/GeoBounds.hpp>

#include <iostream>

int main() {
    using namespace delivery::core;

    // Caja de prueba: un cuadrado ficticio alrededor de Equipetrol.
    // Valores ilustrativos, no son los reales (esos vienen en Etapa 3).
    GeoBounds bounds(
        /*north=*/ -17.7500,
        /*south=*/ -17.7700,
        /*west =*/ -63.2000,
        /*east =*/ -63.1800
    );

    try {
        bounds.validate();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    const int width = 800;
    const int height = 600;

    // Esquina noroeste: debe caer en pixel (0, 0).
    const LatLon nw{bounds.north(), bounds.west()};
    // Esquina sureste: debe caer en pixel (width-1, height-1).
    const LatLon se{bounds.south(), bounds.east()};
    // Centro: debe caer cerca del centro del pixel.
    const LatLon center{(bounds.north() + bounds.south()) / 2.0,
                        (bounds.west() + bounds.east()) / 2.0};

    const Pixel pNW = bounds.toPixel(nw, width, height);
    const Pixel pSE = bounds.toPixel(se, width, height);
    const Pixel pC  = bounds.toPixel(center, width, height);

    std::cout << "NW -> (" << pNW.x << ", " << pNW.y << ")  expected (0, 0)\n";
    std::cout << "SE -> (" << pSE.x << ", " << pSE.y << ")  expected ("
              << (width - 1) << ", " << (height - 1) << ")\n";
    std::cout << "C  -> (" << pC.x << ", " << pC.y << ")  expected (~"
              << (width / 2) << ", ~" << (height / 2) << ")\n";

    // Round-trip: pixel -> geo -> pixel debe devolver el mismo pixel.
    const LatLon back = bounds.fromPixel(pC, width, height);
    const Pixel  rt   = bounds.toPixel(back, width, height);
    std::cout << "Round-trip: (" << rt.x << ", " << rt.y << ")  expected ("
              << pC.x << ", " << pC.y << ")\n";

    // Distancia: NW a SE por Haversine, sanity check.
    const double d = GeoBounds::distanceMeters(nw, se);
    std::cout << "Distance NW->SE: " << d << " m\n";

    return 0;
}