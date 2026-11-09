# Arquitectura del proyecto

> Documento vivo. Se actualiza al cierre de cada etapa. Última actualización: Etapa 3.
> Para una explicación sin tecnicismos, ver [`modules-guide.md`](modules-guide.md).

## 1. Estructura de carpetas

```
EquipeProyecto/
├── CMakeLists.txt            → librería delivery_core, ejecutable delivery_sim, opción ENABLE_TSAN
├── README.md                 → informe final (DP.12)
├── config/equipetrol.json    → configuración de la simulación (mapa real)
├── data/                     → imagen OSM del mapa y polígono del área
├── docs/
│   ├── architecture.md       → este archivo
│   ├── modules-guide.md      → guía de módulos para cualquier lector
│   ├── progress.md           → estado por etapa, problemas conocidos y demoras
│   ├── design-decisions.md   → decisiones de diseño (DD-001 …)
│   └── concept-map.md        → mapa de conceptos (sección 10 del brief)
├── include/delivery/
│   ├── core/                 → Types, Order, CityModel, GeoBounds, StreetNetwork, PathFinder, Rng
│   ├── infra/                → Config, ConfigLoader
│   ├── concurrency/          → OrderBook, ShutdownToken
│   ├── sim/                  → SimClock, OrderGenerator, Courier, Fleet
│   ├── viz/                  → (vacío, Etapa 4)
│   └── app/                  → (vacío, Etapa 9)
├── src/                      → un .cpp por cada .hpp, más main.cpp
├── tests/                    → tests Catch2 y datos de prueba en tests/data/
└── tools/                    → scripts Python para generar y verificar el mapa
```

## 2. Capas y reglas de dependencia

- `core/` no depende de nada del proyecto. No usa hilos, OpenCV ni JSON.
- `infra/` depende de `core/`. `ConfigLoader.cpp` es el único archivo que incluye la librería JSON.
- `concurrency/` depende de `core/`.
- `sim/` depende de `core/`, `infra/` y `concurrency/`.
- `viz/` (Etapa 4) dependerá de `core/` y OpenCV.
- `main.cpp` crea y conecta todo.

## 3. Compilación

| Target | Contenido |
|---|---|
| `delivery_core` | Librería estática con todo el código salvo `main.cpp`. |
| `delivery_sim` | `main.cpp` + `delivery_core` + OpenCV. |
| `delivery_tests` | Tests + `delivery_core` + Catch2. |

- Dependencias por `FetchContent` como archivos comprimidos verificados con SHA256: nlohmann/json 3.11.3 y Catch2 3.5.2.
- `ENABLE_TSAN=ON` añade `-fsanitize=thread` a **todos** los targets. Solo funciona en Linux/WSL: MSYS2 no incluye libtsan (DD-006).
- En Windows, la variable de entorno `CMAKE_GENERATOR=MinGW Makefiles` hace que `cmake ..` genere archivos para `make` (DD-022).

## 4. Hilos (estado actual)

| Hilo | Qué ejecuta | Sobre qué itera | Cómo espera | Quién lo crea / lo une |
|---|---|---|---|---|
| `main` | `main()` | — | `SimClock::waitUntilMs(duración)` | — |
| `tProducer` | `OrderGenerator::run` | Un pedido (o una ráfaga) por intervalo aleatorio | `SimClock::waitUntilMs(próximo pedido)` | `main` / `main` |
| `tConsumer` | `consumerLoop` (provisional, será el despachador en la Etapa 5) | Cada pedido del libro | `OrderBook::waitAndPop` (CV) | `main` / `main` |
| `courier[i]` (uno por repartidor) | `Courier::run` | Cada destino asignado; dentro, cada tramo y cada paso | `cv_.wait` (sin destino) y `SimClock::waitUntilMs` (en movimiento) | `Fleet::start` / `Fleet::join` |
| `renderer` | Etapa 4 | Un cuadro por intervalo | Pausa para fijar los FPS | — |
| `dispatcher` | Etapa 5 | Cada pedido nuevo | `future::wait_for(quoteTimeoutMs)` | — |

## 5. Canales de comunicación y espera

| Canal | Entre | Mecanismo |
|---|---|---|
| `OrderBook` | generador → consumidor | Monitor: `deque` + `mutex` + `condition_variable` |
| `Courier::setTarget` | despachador (Etapa 5) → repartidor | Monitor del repartidor: `stateMutex_` + `cv_` |
| `SimClock` | cualquier hilo ↔ reloj | `cv_.wait_until` hasta la hora real equivalente; `wakeAll()` al apagar |
| `ShutdownToken` | `main` → todos | `std::atomic<bool>`; `waitForStop()` con `mutex` + `condition_variable` |
| Lecturas de estado (`position()`, `state()`, `currentNode()`) | repartidor → render/despachador | Copias tomadas bajo `stateMutex_` |

### Orden de apagado

```
stop.requestStop() → clock.wakeAll() → book.wakeAll() → join(tProducer) → join(tConsumer) → fleet.join()
```
`fleet.join()` despierta a cada repartidor (`Courier::wake`) antes del `join`. Cada `wake`/`wakeAll` toma el mutex correspondiente antes de notificar, para evitar el despertar perdido (DD-017).

## 6. Argumento contra condiciones de carrera, por objeto compartido (fila 10)

| Objeto | Por qué no hay condición de carrera |
|---|---|
| `OrderBook` | `queue_` y `rejectedCount_` solo se leen y escriben con `mutex_` tomado; no se devuelven referencias al interior. |
| `ShutdownToken` | `stop_` es `std::atomic<bool>`: lecturas y escrituras son indivisibles. |
| `SimClock` | Después del constructor solo se leen `timeScale_` y `startReal_`, que son `const`. El `mutex_` existe solo para la condition variable. |
| `Courier` | `state_`, `position_`, `currentNode_`, `pendingTarget_` y `lastIdleMs_` solo se acceden con `stateMutex_`. Las consultas devuelven copias. |
| `StreetNetwork` | Inmutable después de construirse; se comparte como `shared_ptr<const StreetNetwork>`. Solo hay lecturas. |
| `Config` | Se construye antes de crear los hilos y después solo se lee (`const`). |
| `orderIdCounter`, `consumedCount` | `std::atomic<std::uint64_t>` con `fetch_add`. |

Verificado con ThreadSanitizer en cada PR (tests y simulación completa en WSL).

## 7. Historial por etapa

- **Etapa 0:** estructura, herramientas, primer commit.
- **Etapa 1:** `core/`, `infra/`, tests con Catch2.
- **Etapa 2:** `OrderBook`, `ShutdownToken`, `OrderGenerator`; productor-consumidor en `main`.
- **Etapa 3:** `StreetNetwork`, Dijkstra, `SimClock`, `Courier`, `Fleet`. Corrección posterior (PR #6 a #13): despertares perdidos, apagado de la flota, teletransporte, reloj único, CMake y tests.
