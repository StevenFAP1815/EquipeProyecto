# Arquitectura del proyecto

> Documento vivo. Se actualiza al cierre de cada etapa.

## 1. Estructura de carpetas

equipetrol-delivery/
├── CMakeLists.txt
├── README.md
├── config/          → configuraciones JSON de simulación
├── data/            → imagen OSM del mapa
├── docs/            → documentación (este archivo, decisiones, conceptos)
├── include/delivery/
│   ├── core/        → dominio puro (Order, Courier, Restaurant, StreetNetwork)
│   ├── infra/       → I/O (JSON, imagen, logger)
│   ├── concurrency/ → monitores, CVs, futures, locks
│   ├── sim/         → orquestación del ciclo de vida
│   ├── viz/         → render con OpenCV
│   └── app/         → bootstrap, señales, Application
├── src/             → main.cpp
├── tools/           → scripts de desarrollo (OSM → config)
├── experiments/     → configs y resultados de DP.10
└── tests/           → pruebas

## 2. Capas y reglas de dependencia

- `core/` no depende de nada del proyecto. Cero `<thread>`, cero `<mutex>`.
- `infra/` depende de `core/`.
- `concurrency/` depende de `core/`.
- `sim/` depende de `core/`, `infra/`, `concurrency/`.
- `viz/` depende de `core/`. Se comunica con `sim/` por snapshot inmutable.
- `app/` depende de todo.
- `main.cpp` solo llama a `app/`.

## 3. Toolchain

- Windows: MSYS2 UCRT64 (g++, cmake, make, OpenCV).
- Linux (para DP.11): WSL2 + Ubuntu.
- JSON: nlohmann/json v3.11.3 (FetchContent).

## 4. Diseño de hilos (a implementar)

| Hilo | Rol | Etapa |
|------|-----|-------|
| main | bootstrap, señales, join | 9 |
| order-gen | genera pedidos | 2 |
| dispatcher | asigna pedidos | 5 |
| courier[i] | actor por moto | 3 |
| renderer | dibuja a FPS fijo | 4 |

## 5. Canales de comunicación (a implementar)

- OrderBook: monitor con mutex + CV.
- RestaurantQueue: slots + FIFO con mutex + CV.
- CourierBag: capacidad con mutex.
- Dispatcher: futures + timeout.
- Shutdown: atomic + CV con predicado.

## 6. Decisiones de señales y render

Ver `docs/design-decisions.md` (DD-004, DD-005).