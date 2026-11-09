# Estado del proyecto

> Qué está hecho, qué falta y qué problemas hay. Se actualiza al cerrar cada etapa.
> Última actualización: 09/11/2026, cierre de la Etapa 3.

## Etapas del plan y criterios de la rúbrica

El brief del curso define los criterios **DP.0 a DP.12**. Las **etapas** son el plan de trabajo propio para cumplirlos; los números no coinciden uno a uno.

| Etapa | Contenido | Criterio | Puntos | Estado |
|---|---|---|---|---|
| 0 | Estructura del proyecto, herramientas, compilación | DP.0 | Puerta | ✅ Hecho |
| 1 | Conceptos básicos (`core/`), lectura y validación del JSON, tests | DP.1 (configuración) | 4 | ✅ Hecho |
| 2 | Libro de pedidos, señal de apagado, generador de pedidos | DP.2 | 6 | ✅ Hecho (falta el rechazo `unreachable`, Etapa 5) |
| 3 | Mapa de calles, Dijkstra, reloj simulado, repartidores, flota | DP.1 (ciudad), DP.3 | 6 + 14 | ✅ Hecho |
| 4 | Ventana con el mapa y los puntos en movimiento (OpenCV) | DP.8 | 6 | ⏳ Siguiente |
| 5 | Despachador: estimaciones simultáneas con `future` y límite de tiempo | DP.4 | 8 | ⏳ Pendiente |
| 6 | Cupos de recogida en restaurantes (orden de llegada), capacidad de la bolsa | DP.5 | 8 | ⏳ Pendiente |
| 7 | Averías y transferencia todo-o-nada de pedidos | DP.6 | 8 | ⏳ Pendiente |
| 8 | Registro de eventos JSON y resumen final exacto | DP.7 | 8 | ⏳ Pendiente (ya existe la línea de inicio) |
| 9 | Señales SIGINT/SIGTERM y apagado en menos de 5 s | DP.9 | 6 | ⏳ Pendiente |
| 10 | Análisis con herramientas (ThreadSanitizer, Valgrind) | DP.11 + "Sin condiciones de carrera" | 6 + 8 | 🔶 En curso: TSan ya se usa en cada PR |
| 11 | Experimentos y tabla comparativa | DP.10 | 6 | ⏳ Pendiente |
| 12 | Informe final, diagrama de hilos y canales, mapa de conceptos | DP.12 | 6 | 🔶 En curso |

## Verificaciones al cierre de la Etapa 3

| Verificación | Resultado |
|---|---|
| `mkdir build && cd build && cmake .. && make` (Windows, desde cero) | ✅ Sin warnings con `-Wall -Wextra -Wpedantic` |
| Tests automáticos (`ctest`) | ✅ 44 de 44 |
| `cmake -DENABLE_TSAN=ON ..` (Ubuntu en WSL) | ✅ Compila; 0 reportes en tests y en la simulación completa |
| Simulación con `config/equipetrol.json` | ✅ Arranca 11 hilos, imprime `Simulation has started...` y termina con código 0 |
| Configuraciones inválidas | ✅ Mensaje en stderr y código 1, sin simular |

## Problemas conocidos (pendientes)

| Problema | Impacto | Plan |
|---|---|---|
| El mapa no es *fuertemente conexo*: por las calles de un solo sentido, 6 de 34 intersecciones no se alcanzan desde el punto de partida; los restaurantes r1 y r5 son inalcanzables y r3 es un callejón sin salida. | Pedidos imposibles de entregar. | Etapa 5: rechazar esos pedidos con `unreachable`. Más adelante: regenerar el mapa. |
| La imagen del mapa no muestra el Cuarto Anillo y las intersecciones se concentran entre el Segundo y el Tercer Anillo. | Puede restar puntos en DP.1 ("la ciudad es real"). | Ampliar `data/area.geojson` y regenerar el mapa (después de la Etapa 5). |
| Todavía no hay despachador: un hilo provisional saca los pedidos del libro y solo los cuenta. Los repartidores existen y se mueven, pero aún no reciben pedidos. | El resumen todavía no tiene el formato de la sección 6.3. | Etapas 5 y 8. |
| `--log <ruta>` aparece en el mensaje de uso pero aún no está implementado. | — | Etapa 8, junto con el registro de eventos. |
| En Windows, SIGTERM no lo envía el sistema operativo. | Solo se puede probar con Ctrl+C (SIGINT). | Etapa 9: probar SIGTERM en Ubuntu (WSL) y documentarlo. |
| ThreadSanitizer y Valgrind no existen para el compilador de Windows (MSYS2). | El análisis se hace en Ubuntu (WSL). | Documentado en DD-006. Valgrind se instalará en la Etapa 10. |

## Demoras y sus causas

| Qué se demoró | Por qué |
|---|---|
| Comenzar la Etapa 4 | La Etapa 3 se había integrado sin tests y con errores de concurrencia. Antes de avanzar hubo que corregirlos (PR #6 a #12): un *despertar perdido* que podía colgar el programa al apagarse, repartidores que nunca recibían la señal de apagado, repartidores que "saltaban" al punto de partida y dos relojes simulados distintos. |
| Compilación con los comandos exactos del brief | En Windows, CMake usaba Ninja por defecto, así que `cmake .. && make` fallaba. Además, `cmake ..` tardaba más de 15 minutos porque descargaba repositorios completos. Se corrigió con la variable `CMAKE_GENERATOR` y descargando archivos comprimidos con hash. |
| Mapa | El script que genera el mapa conservaba intersecciones mirando las calles como si fueran de doble sentido; con los sentidos reales, parte del mapa queda aislada. Se detectó tarde porque el cargador no comprueba la conectividad. |
| Archivo de tests de la Etapa 3 | Nunca se subió a Git y se perdió; hubo que reescribirlo. Lección: hacer *commit* en cuanto los tests funcionan. |
