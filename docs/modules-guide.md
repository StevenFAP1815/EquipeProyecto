# Guía de módulos

> Para cualquier lector, sepa o no programar. Explica **qué hace** cada parte del proyecto y **para qué sirve**, sin entrar en el código.

## La idea en una frase

El programa simula una empresa de delivery en la zona Equipetrol (Santa Cruz de la Sierra). Aparecen pedidos, motociclistas repartidores los recogen en restaurantes y los llevan a los clientes. **Todo ocurre al mismo tiempo**, como en la vida real, y eso es justamente lo que el proyecto estudia: la *concurrencia*.

## Dos palabras clave

- **Hilo (*thread*):** un "trabajador" dentro del programa. Varios hilos trabajan a la vez, como varias personas en una oficina. En este proyecto cada motociclista es un hilo, el generador de pedidos es otro hilo, y así.
- **Recurso compartido:** algo que varios hilos usan, por ejemplo el libro de pedidos. Si dos hilos lo tocan exactamente al mismo tiempo sin coordinarse, los datos se corrompen. Por eso existen los **candados** (*mutex*): solo un hilo a la vez puede tener el candado de un recurso.

## Mapa de carpetas

```
EquipeProyecto/
├── config/      → archivos de configuración de la simulación (JSON)
├── data/        → imagen del mapa y polígono de la zona
├── docs/        → documentación (este archivo y otros)
├── include/     → "fichas técnicas" de cada módulo (archivos .hpp)
├── src/         → el funcionamiento de cada módulo (archivos .cpp)
├── tests/       → pruebas automáticas que verifican que todo funciona
├── tools/       → scripts de apoyo en Python (generar y revisar el mapa)
├── CMakeLists.txt → receta para compilar el programa
└── README.md    → informe principal del proyecto
```

Cada módulo tiene dos archivos con el mismo nombre: uno en `include/` (qué ofrece) y otro en `src/` (cómo lo hace).

## Los módulos, capa por capa

El código está organizado en **capas**. Cada capa solo usa las capas de más abajo, nunca las de arriba. Esto mantiene el orden: si algo falla, se sabe dónde buscar.

```
   main          (arranca y detiene todo)
     │
    viz          (la ventana: solo mira, no cambia nada)
     │
    sim          (los actores de la simulación)
     │
 concurrency     (herramientas para coordinar hilos)
     │
   infra         (leer la configuración)
     │
   core          (los conceptos básicos del mundo)
```

### `core/` — los conceptos básicos

| Módulo | Para qué sirve |
|---|---|
| `Types` | Da un "tipo" distinto a cada identificador (pedido, repartidor, calle…) para que no se puedan confundir por error. |
| `Order` | Describe un pedido y sus estados: creado → asignado → recogido → entregado (o rechazado / pendiente). |
| `CityModel` | Describe intersecciones, calles y restaurantes. |
| `GeoBounds` | Convierte coordenadas reales (latitud/longitud) en posiciones de la imagen del mapa, y calcula distancias en metros. |
| `StreetNetwork` | El mapa de calles como una red: qué intersección conecta con cuál, respetando las calles de un solo sentido. |
| `PathFinder` | Encuentra el camino más corto entre dos puntos (algoritmo de Dijkstra). |
| `Rng` | Ayuda a que lo "aleatorio" sea repetible: con la misma semilla, la misma simulación. |

### `infra/` — la configuración

| Módulo | Para qué sirve |
|---|---|
| `Config` | Guarda todos los parámetros de la simulación (cuántos repartidores, velocidad, duración…). |
| `EventLog` | El **registro de eventos**: escribe cada cosa que pasa (pedido creado, asignado, entregado, avería…) como una línea en un archivo, y lleva las cuentas para el resumen final. Tiene su propio candado, así dos hilos nunca mezclan sus líneas. |
| `ConfigLoader` | Lee el archivo JSON, lo revisa y, si algo está mal, muestra un mensaje claro y el programa no arranca. |

### `concurrency/` — coordinar hilos

| Módulo | Para qué sirve |
|---|---|
| `OrderBook` | El **libro de pedidos**: una fila de pedidos en espera, con capacidad máxima. Un hilo deja pedidos y otro los saca; si no hay pedidos, el que saca **espera dormido** sin gastar procesador. |
| `ShutdownToken` | La **señal de apagado**: una bandera que todos los hilos consultan para saber cuándo terminar. |
| `PickupDesk` | El **mostrador de un restaurante**: tiene un número limitado de lugares para recoger comida. Cada repartidor que llega toma un número, como en una panadería, y espera su turno y un lugar libre. Así se atiende en orden de llegada y nunca hay más repartidores recogiendo que lugares. |
| `ChangeSignal` | Un **aviso de "algo cambió"**: un hilo puede dormir hasta que otro avise. Lo usan los repartidores para avisar "quedé libre" a quien reparte los pedidos. |

### `sim/` — los actores

| Módulo | Para qué sirve |
|---|---|
| `SimClock` | El **reloj simulado**: hace que el tiempo pase más rápido (por ejemplo, 1 segundo real = 60 segundos simulados) y permite a los hilos "dormir hasta cierta hora". |
| `OrderGenerator` | Crea pedidos a intervalos aleatorios, a veces en ráfagas, y los deja en el libro de pedidos. |
| `Courier` | Un **repartidor** con una **bolsa** de capacidad limitada: espera a que le asignen un pedido y va al restaurante; mientras va o espera, puede sumar más pedidos **del mismo restaurante** hasta llenar la bolsa. Hace fila en el mostrador, espera la comida, y entrega los pedidos uno por uno moviéndose paso a paso por las calles. Al terminar avisa que quedó libre. A veces **se avería** a mitad de camino: pide a un compañero libre que se lleve sus pedidos; el compañero los toma todos de una vez y los entrega. Si nadie puede ayudar antes de que termine la reparación, el mismo repartidor, ya reparado, los entrega. |
| `Dispatcher` | El **despachador**: toma cada pedido del libro. Si no se puede entregar por las calles, lo rechaza (`unreachable`). Si se puede, pide **al mismo tiempo** una estimación de cuánto tardaría a los repartidores libres más cercanos, espera las respuestas un tiempo limitado y elige al más rápido. Si nadie puede tomarlo a tiempo, lo rechaza (`noCourier`). |
| `Fleet` | La **flota**: crea a todos los repartidores, les da un hilo a cada uno y al final espera a que todos terminen. |

### `viz/` — la ventana

| Módulo | Para qué sirve |
|---|---|
| `Renderer` | Abre la ventana y unas 30 veces por segundo dibuja el mapa, las calles, los restaurantes y a cada repartidor como un punto de color según lo que está haciendo (gris: libre; azul: yendo al restaurante; etc.). Muestra un panel con contadores y la atribución de OpenStreetMap. Si se pulsa ESC o se cierra la ventana, pide el apagado. |

### `main.cpp` — el director de orquesta

Lee la configuración, crea todos los módulos, arranca los hilos, espera a que se acabe el tiempo o llegue una señal de parada (Ctrl+C, SIGTERM o cerrar la ventana) y luego detiene todo **en orden**, sin dejar ningún hilo abandonado.

## Las pruebas (`tests/`)

Programas que ejecutan cada módulo con casos conocidos y comprueban el resultado. Por ejemplo: "si pido el apagado, ¿todos los hilos terminan en menos de 2 segundos?". Se ejecutan con `ctest`. También se pasan por **ThreadSanitizer**, una herramienta que detecta cuando dos hilos tocan el mismo dato sin coordinarse.

## Las herramientas (`tools/`)

| Script | Para qué sirve |
|---|---|
| `osm_to_config.py` | Descarga las avenidas y restaurantes reales de OpenStreetMap dentro de `data/area.geojson` (del 2.º al 4.º Anillo), se queda solo con la parte del mapa donde se puede ir y volver entre cualquier par de puntos respetando los sentidos, añade puntos en las curvas para que las motos sigan la calle, y genera `config/equipetrol.json` y la imagen del mapa. |
| `check_event_log.py` | Revisa un registro de eventos y el resumen: cada línea es JSON válido, cada pedido sigue el orden de estados y termina en un solo estado final, ningún pedido está en manos de dos repartidores y las cuentas coinciden. |
| `run_experiment.py` | Repite la simulación cambiando un solo parámetro (por ejemplo, la cantidad de repartidores) con varias semillas, guarda los datos de cada ejecución y arma la tabla comparativa. |
| `run_with_signal.sh` | Ejecuta la simulación, espera la línea de inicio y envía Ctrl+C (SIGINT) a los N segundos, guardando todas las salidas. Se usa para el análisis con herramientas. |
| `tsan_canary.cpp` | Programa mínimo con una condición de carrera a propósito: demuestra que las supresiones de ThreadSanitizer no esconden errores del proyecto. |
| `verify_alignment.py` | Dibuja las calles del JSON sobre la imagen para comprobar visualmente que coinciden. |

## Qué falta

Ver [`progress.md`](progress.md): tabla de etapas, qué está hecho y qué falta.
