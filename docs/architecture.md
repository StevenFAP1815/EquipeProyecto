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
│   ├── viz/                  → Renderer (ventana OpenCV)
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
- `viz/` depende de `core/`, `concurrency/`, `sim/` (solo lectura) y OpenCV. Se compila dentro de `delivery_sim`, no en `delivery_core`, para que los tests no necesiten OpenCV.
- `main.cpp` crea y conecta todo.

## 3. Compilación

| Target | Contenido |
|---|---|
| `delivery_core` | Librería estática con todo el código salvo `main.cpp`. |
| `delivery_sim` | `main.cpp` + `viz/Renderer.cpp` + `delivery_core` + OpenCV. |
| `delivery_tests` | Tests + `delivery_core` + Catch2. |

- Dependencias por `FetchContent` como archivos comprimidos verificados con SHA256: nlohmann/json 3.11.3 y Catch2 3.5.2.
- `ENABLE_TSAN=ON` añade `-fsanitize=thread` a **todos** los targets. Solo funciona en Linux/WSL: MSYS2 no incluye libtsan (DD-006).
- En Windows, la variable de entorno `CMAKE_GENERATOR=MinGW Makefiles` hace que `cmake ..` genere archivos para `make` (DD-022).

## 4. Hilos (estado actual)

| Hilo | Qué ejecuta | Sobre qué itera | Cómo espera | Quién lo crea / lo une |
|---|---|---|---|---|
| `main` | `main()` | — | `SimClock::waitUntilMs(duración)` | — |
| `tProducer` | `OrderGenerator::run` | Un pedido (o una ráfaga) por intervalo aleatorio | `SimClock::waitUntilMs(próximo pedido)` | `main` / `main` |
| `tDispatcher` | `Dispatcher::run` | Cada pedido del libro: comprueba que sea alcanzable, pide estimaciones y asigna | `OrderBook::waitAndPop` (CV), `future::wait_until(quoteTimeoutMs)` y, si no hay repartidor, `freedSignal.waitForChange` hasta `acceptTimeoutMs` | `main` / `main` |
| estimación (temporal, hasta `max(2, hardware_concurrency)` por pedido) | `Dispatcher::estimateDeliveryMs` vía `std::async(std::launch::async, …)` | Una sola estimación: Dijkstra repartidor→restaurante y restaurante→cliente | No espera: calcula y termina | `std::async` / el destructor del `future` en `chooseCourier` |
| `courier[i]` (uno por repartidor) | `Courier::run` | Cada pedido asignado: ir al restaurante, esperar la comida, ir al cliente; dentro, cada tramo y cada paso | `cv_.wait` (sin pedido) y `SimClock::waitUntilMs` (en movimiento) | `Fleet::start` / `Fleet::join` |
| `tRenderer` | `Renderer::run` | Un cuadro cada ~33 ms: copia el fondo, lee posición y estado de cada repartidor (bajo su candado) y dibuja | `cv::waitKey(33)`: pausa para fijar los FPS, permitida por el brief | `main` / `main` |

## 5. Canales de comunicación y espera

| Canal | Entre | Mecanismo |
|---|---|---|
| `OrderBook` | generador → despachador | Monitor: `deque` + `mutex` + `condition_variable` |
| `ChangeSignal freedSignal` | repartidor → despachador | Contador de versión + `mutex` + `condition_variable`: el repartidor hace `notify()` al quedar libre; el despachador espera con `waitForChange` |
| `std::future<std::int64_t>` | hilo de estimación → despachador | Canal de un solo uso con el resultado o la excepción |
| `PickupDesk` (uno por restaurante, creado por `Fleet`) | repartidor ↔ repartidores del mismo restaurante | Monitor con tickets: `enter()` da un número de llegada y espera turno y cupo; `leave()` libera el cupo. FIFO y nunca más de `pickupSlots` |
| `Courier::tryAssign` | despachador → repartidor | Monitor del repartidor: `stateMutex_` + `cv_` |
| `SimClock` | cualquier hilo ↔ reloj | `cv_.wait_until` hasta la hora real equivalente; `wakeAll()` al apagar |
| `ShutdownToken` | `main` → todos | `std::atomic<bool>`; `waitForStop()` con `mutex` + `condition_variable` |
| Lecturas de estado (`position()`, `state()`, `currentNode()`) | repartidor → render/despachador | Copias tomadas bajo `stateMutex_` |

### Orden de apagado

```
stop.requestStop() → clock.wakeAll() → book.wakeAll() → freedSignal.wakeAll() → join(tProducer) → join(tDispatcher) → join(tRenderer) → fleet.join()
```
Si el usuario pulsa ESC o cierra la ventana, `Renderer::run` llama a `stop.requestStop()` y `clock.wakeAll()`: `main` despierta de su espera y sigue exactamente el mismo orden de apagado.

`fleet.join()` despierta a cada repartidor (`Courier::wake`) y a cada mostrador (`PickupDesk::wakeAll`) antes del `join`. Cada `wake`/`wakeAll` toma el mutex correspondiente antes de notificar, para evitar el despertar perdido (DD-017).

## 6. Argumento contra condiciones de carrera, por objeto compartido (fila 10)

| Objeto | Por qué no hay condición de carrera |
|---|---|
| `OrderBook` | `queue_` y `rejectedCount_` solo se leen y escriben con `mutex_` tomado; no se devuelven referencias al interior. |
| `ShutdownToken` | `stop_` es `std::atomic<bool>`: lecturas y escrituras son indivisibles. |
| `SimClock` | Después del constructor solo se leen `timeScale_` y `startReal_`, que son `const`. El `mutex_` existe solo para la condition variable. |
| `Courier` | `state_`, `position_`, `currentNode_`, `job_` (el pedido que lleva), `delivered_`, `failed_` y `lastIdleMs_` solo se acceden con `stateMutex_`. Las consultas devuelven copias. |
| `ChangeSignal` | `version_` solo se lee y se escribe con `mutex_`. |
| `PickupDesk` | `busy_`, `nextTicket_` y `nowServing_` solo con `mutex_`. El mapa de mostradores de `Fleet` se llena antes de crear los hilos y después solo se lee. |
| `Dispatcher` | Sus contadores son `std::atomic`. Los hilos de estimación solo leen datos inmutables (`StreetNetwork`, `Config`), su propia copia de `QuoteRequest` y el repartidor a través de métodos con candado (`currentNode()`, `speedKmh()` es `const`). El resultado vuelve por el `future`, no por memoria compartida. |
| `StreetNetwork` | Inmutable después de construirse; se comparte como `shared_ptr<const StreetNetwork>`. Solo hay lecturas. |
| `Config` | Se construye antes de crear los hilos y después solo se lee (`const`). |
| `Renderer` | Su imagen de fondo solo la usa el hilo `tRenderer`. Lee a los demás únicamente por métodos que toman su candado (`Courier::position/state`, `OrderBook::size/rejectedCount`) o por atómicos (`orderIdCounter`). |
| `orderIdCounter`, `consumedCount` | `std::atomic<std::uint64_t>` con `fetch_add`. |

Verificado con ThreadSanitizer en cada PR (tests y simulación completa en WSL). Con la ventana abierta, TSan reporta avisos **dentro de Qt y dbus** (bibliotecas del sistema que usa OpenCV en Linux): ver [`progress.md`](progress.md#problemas-conocidos-pendientes).

## 7. Historial por etapa

- **Etapa 0:** estructura, herramientas, primer commit.
- **Etapa 1:** `core/`, `infra/`, tests con Catch2.
- **Etapa 2:** `OrderBook`, `ShutdownToken`, `OrderGenerator`; productor-consumidor en `main`.
- **Etapa 3:** `StreetNetwork`, Dijkstra, `SimClock`, `Courier`, `Fleet`. Corrección posterior (PR #6 a #13): despertares perdidos, apagado de la flota, teletransporte, reloj único, CMake y tests.
