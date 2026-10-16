# Arquitectura del proyecto

> Documento vivo. Se actualiza al cierre de cada etapa.

## 1. Estructura de carpetas

equipetrol-delivery/
├── CMakeLists.txt          → targets delivery_sim y delivery_tests
├── README.md               → informe final (DP.12)
├── config/                 → configuraciones JSON de simulacion
│   └── equipetrol.json
├── data/                   → imagen OSM del mapa (Etapa 3)
├── docs/                   → documentacion
│   ├── architecture.md     → este archivo
│   ├── design-decisions.md → ADRs (DD-001..)
│   ├── concept-map.md      → mapa de conceptos (seccion 10)
│   ├── experiments.md      → DP.10 (Etapa 11)
│   └── tools-analysis.md   → DP.11 (Etapa 10)
├── include/delivery/
│   ├── core/               → dominio puro
│   │   ├── Types.hpp       → strong typedefs de IDs
│   │   ├── GeoBounds.hpp   → conversion lat/lon <-> pixel
│   │   └── CityModel.hpp   → Node, Street, Restaurant
│   ├── infra/              → I/O
│   │   ├── Config.hpp      → struct Config con sub-secciones
│   │   └── ConfigLoader.hpp→ carga + validacion + jerarquia de errores
│   ├── concurrency/        → (vacio, Etapa 2+)
│   ├── sim/                → (vacio, Etapa 2+)
│   ├── viz/                → (vacio, Etapa 4)
│   └── app/                → (vacio, Etapa 9)
├── src/
│   ├── main.cpp            → orquesta carga de config
│   ├── core/GeoBounds.cpp
│   └── infra/ConfigLoader.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_geobounds.cpp
│   ├── test_config_loader.cpp
│   └── data/               → fixtures JSON
├── tools/                  → scripts de desarrollo (Etapa 3)
└── experiments/            → DP.10 (Etapa 11)

## 2. Capas y reglas de dependencia

- `core/` no depende de nada del proyecto. Cero `<thread>`, cero `<mutex>`,
  cero OpenCV, cero `<nlohmann/json>`.
- `infra/` depende de `core/`. ConfigLoader es la **unica** TU que incluye
  `<nlohmann/json>` (DD-009).
- `concurrency/` (Etapa 2+) dependera de `core/`.
- `sim/` (Etapa 2+) dependera de `core/`, `infra/`, `concurrency/`.
- `viz/` (Etapa 4) dependera de `core/` y OpenCV.
- `app/` (Etapa 9) dependera de todo.
- `main.cpp` solo llama a `app/` o `infra/ConfigLoader` directamente.

## 3. Toolchain

- Windows: MSYS2 UCRT64, Ninja como generador, g++ con C++17.
- Linux (para DP.11): WSL2 + Ubuntu.
- JSON: nlohmann/json v3.11.3 (FetchContent).
- Tests: Catch2 v3.5.2 (FetchContent, fallback a find_package).
- OpenCV: encontrado por find_package, componentes core/highgui/imgproc/imgcodecs.

### ThreadSanitizer

El toolchain MSYS2/MinGW UCRT64 no provee `libtsan`. La opcion
`-DENABLE_TSAN=ON` detecta esta situacion y falla con un mensaje explicativo.
El analisis con ThreadSanitizer (DP.11) se ejecuta en WSL2 con Ubuntu,
donde el codigo compila sin cambios.

## 4. Diseño de hilos (a implementar)

| Hilo | Rol | Etapa |
|------|-----|-------|
| main | bootstrap, senales, join | 9 |
| order-gen | genera pedidos | 2 |
| dispatcher | asigna pedidos | 5 |
| courier[i] | actor por moto | 3 |
| renderer | dibuja a FPS fijo | 4 |

## 5. Canales de comunicacion (a implementar)

- OrderBook: monitor con mutex + CV (Etapa 2).
- RestaurantQueue: slots + FIFO con mutex + CV (Etapa 6).
- CourierBag: capacidad con mutex (Etapa 6).
- Dispatcher: futures + timeout (Etapa 5).
- Shutdown: atomic + CV con predicado (Etapa 9).

## 6. Estado por etapa

- Etapa 0: bootstrap, toolchain verificado, primer commit.
- Etapa 1: dominio (core/), config (infra/), tests automatizados con Catch2,
  20 tests passing, cero warnings.