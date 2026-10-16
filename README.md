# Equipetrol Delivery

Simulación concurrente de despacho de delivery sobre la Zona Equipetrol
(Santa Cruz de la Sierra, Bolivia), construida en C++17.

> Proyecto de Concurrencia en C++.

## Atribución del mapa

© OpenStreetMap contributors

Los datos de OpenStreetMap se publican bajo la licencia ODbL.
Ver: https://www.openstreetmap.org/copyright

## Dependencias


| Componente | Versión | Uso |
|------------|---------|-----|
| C++ | 17 | Estándar del proyecto |
| CMake | ≥ 3.16 | Sistema de build |
| Ninja | ≥ 1.11 | Generador (alternativa: Make) |
| g++ (MSYS2 UCRT64) | ≥ 13 | Compilador |
| OpenCV | 4.x | Ventana y renderizado (solo Etapa 4+) |
| nlohmann/json | 3.11.3 | Lectura y escritura JSON (vía FetchContent) |
| Catch2 | 3.5.2 | Tests (vía FetchContent, solo target `delivery_tests`) |
| Python | 3.x | Script `tools/osm_to_config.py` (solo desarrollo, no runtime) |


## Compilación

```bash
mkdir build && cd build
cmake ..
cmake --build .        # o: ninja, o: make

## Opción: ThreadSanitizer
cmake -DENABLE_TSAN=ON ..
cmake --build . 

## Tests
cd build
ctest --output-on-failure

## Ejecución

./delivery_sim <config.json> [--log <path>]
Ejemplo: ./delivery_sim ../config/equipetrol.json

> TODO (Etapa 0)

## Arquitectura

Ver [`docs/architecture.md`](docs/architecture.md).

## Esquema de hilos

> TODO (DP.12)

## Mapa de conceptos

Ver [`docs/concept-map.md`](docs/concept-map.md).

## Experimentos

Ver [`docs/experiments.md`](docs/experiments.md).

## Análisis con herramientas

Ver [`docs/tools-analysis.md`](docs/tools-analysis.md).

## Licencia

> TODO