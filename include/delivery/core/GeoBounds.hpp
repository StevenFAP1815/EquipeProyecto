#pragma once

#include <cstdint>
#include <utility>

namespace delivery::core {

// Coordenada geografica simple. Grados decimales.
struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

// Coordenada en pixeles de la imagen del mapa. Origen: esquina superior izquierda.
struct Pixel {
    int x = 0;
    int y = 0;
};

// Caja geografica del mapa. Las cuatro esquinas en grados decimales.
//
// Invariantes (verificados por validate()):
//   north > south
//   east  > west
//
// La conversion a pixeles usa proyeccion equirectangular, suficiente para un
// area urbana de pocos kilometros (curvatura terrestre despreciable).
class GeoBounds {
public:
    GeoBounds() = default;
    GeoBounds(double north, double south, double west, double east);

    double north() const { return north_; }
    double south() const { return south_; }
    double west()  const { return west_;  }
    double east()  const { return east_;  }

    // Lanza std::invalid_argument si north <= south o east <= west.
    void validate() const;

    // Convierte lat/lon a pixel en una imagen de width x height.
    // Fuera de los limites de la caja: el pixel puede quedar fuera de la imagen,
    // no se recorta (el llamador decide si clampear).
    Pixel toPixel(const LatLon& geo, int width, int height) const;

    // Conversion inversa. Util para debugging y tests de round-trip.
    LatLon fromPixel(const Pixel& px, int width, int height) const;

    // Distancia aproximada en metros entre dos coordenadas.
    // Usa la formula de Haversine (esferica). Suficiente a escala de ciudad.
    static double distanceMeters(const LatLon& a, const LatLon& b);

private:
    double north_ = 0.0;
    double south_ = 0.0;
    double west_  = 0.0;
    double east_  = 0.0;
};

}  // namespace delivery::core