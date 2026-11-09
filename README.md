# Equipetrol Delivery

Simulación concurrente de despacho de pedidos de comida sobre la Zona Equipetrol (Santa Cruz de la Sierra, Bolivia), escrita en C++17. Trabajo final del curso de Concurrencia en C++.

Varios hilos trabajan al mismo tiempo: un generador de pedidos, un hilo por cada motociclista repartidor y, en próximas etapas, un despachador y una ventana que dibuja el mapa en vivo.

> **Estado:** Etapa 4 (ventana) terminada. Qué está hecho, qué falta y los problemas conocidos están en [`docs/progress.md`](docs/progress.md).

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/modules-guide.md`](docs/modules-guide.md) | Qué hace cada módulo, explicado para cualquier lector. |
| [`docs/progress.md`](docs/progress.md) | Etapas, criterios de la rúbrica, problemas conocidos y demoras. |
| [`docs/architecture.md`](docs/architecture.md) | Capas, hilos, canales de comunicación y argumento contra condiciones de carrera. |
| [`docs/concept-map.md`](docs/concept-map.md) | Mapa de conceptos (sección 10 del brief). |
| [`docs/design-decisions.md`](docs/design-decisions.md) | Registro de decisiones de diseño (DD-001 …). |

## Compilación

```bash
mkdir build && cd build
cmake .. && make
```

La primera configuración descarga nlohmann/json y Catch2 (unos 1,2 MB), por eso necesita internet.

**Windows (MSYS2 UCRT64):** CMake usa Ninja por defecto. Para que `cmake ..` genere archivos para `make`, la variable de entorno de usuario `CMAKE_GENERATOR` vale `MinGW Makefiles` (ver DD-022). En PowerShell:

```powershell
[Environment]::SetEnvironmentVariable("CMAKE_GENERATOR", "MinGW Makefiles", "User")
```

### ThreadSanitizer

```bash
mkdir build-tsan && cd build-tsan
cmake -DENABLE_TSAN=ON .. && make
```

ThreadSanitizer no existe para el compilador de MSYS2, por eso se ejecuta en **Ubuntu (WSL2)** (ver DD-006). La opción instrumenta el programa y los tests.

## Ejecución

```bash
./delivery_sim ../config/equipetrol.json
```

El programa imprime `Simulation has started...` cuando todos los hilos están en marcha y un resumen JSON al terminar. Con una configuración inválida muestra un mensaje en stderr y sale con código distinto de cero, sin simular.

Se abre una ventana con el mapa y los repartidores en movimiento. Pulsar ESC o cerrar la ventana termina la simulación de forma ordenada. La opción `--log <ruta>` llega en la Etapa 8.

## Tests

```bash
cd build
ctest --output-on-failure
```

## Dependencias (versiones usadas)

| Componente | Versión | Uso |
|---|---|---|
| C++ | 17 | Estándar del proyecto |
| g++ (MSYS2 UCRT64, Windows) | 16.2.0 | Compilador |
| g++ (Ubuntu 26.04, WSL2) | 15.2.0 | Compilador para ThreadSanitizer |
| CMake | 4.4.3 (Windows), 4.2.3 (WSL) | Sistema de compilación (mínimo 3.16) |
| GNU Make | 4.4.1 | Construcción |
| OpenCV | 5.0.0 (Windows), 4.10.0 (WSL) | Ventana y dibujo (desde la Etapa 4) |
| nlohmann/json | 3.11.3 | Lectura de la configuración JSON |
| Catch2 | 3.5.2 | Tests (solo en `delivery_tests`) |
| Python | 3.13 | Scripts de `tools/` para generar el mapa (solo desarrollo) |

## Mapa

El mapa, las calles y los restaurantes provienen de OpenStreetMap y se generan con `tools/osm_to_config.py`.

**© OpenStreetMap contributors.** Los datos de OpenStreetMap se publican bajo la licencia ODbL: https://www.openstreetmap.org/copyright

## Uso de inteligencia artificial

Durante el desarrollo usé **Claude Code** (Anthropic) como asistente de programación:

- Revisar el código y detectar errores. Por ejemplo, el *despertar perdido* al apagar y el "teletransporte" de los repartidores.
- Proponer y aplicar correcciones, cada una con sus tests, en Pull Requests pequeños que revisé antes de integrarlos.
- Configurar la compilación, ejecutar ThreadSanitizer y redactar documentación.

Revisé y aprobé cada cambio antes de integrarlo. Cada Pull Request tiene notas de estudio con la explicación de los conceptos aplicados, para poder explicar cada línea del código.

## Pendiente para el informe final (DP.12)

- Diagrama de hilos y canales: versión actual en tablas en [`docs/architecture.md`](docs/architecture.md); el diagrama completo llega en la Etapa 12.
- Experimentos (DP.10): Etapa 11.
- Resultados del análisis con herramientas (DP.11): Etapa 10.
