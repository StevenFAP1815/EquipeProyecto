# Estado del proyecto

> Qué está hecho, qué falta y qué problemas hay. Se actualiza al cerrar cada etapa.
> Última actualización: 09/11/2026, Etapa 5 (despachador).

## Etapas del plan y criterios de la rúbrica

El brief del curso define los criterios **DP.0 a DP.12**. Las **etapas** son el plan de trabajo propio para cumplirlos; los números no coinciden uno a uno.

| Etapa | Contenido | Criterio | Puntos | Estado |
|---|---|---|---|---|
| 0 | Estructura del proyecto, herramientas, compilación | DP.0 | Puerta | ✅ Hecho |
| 1 | Conceptos básicos (`core/`), lectura y validación del JSON, tests | DP.1 (configuración) | 4 | ✅ Hecho |
| 2 | Libro de pedidos, señal de apagado, generador de pedidos | DP.2 | 6 | ✅ Hecho (falta el rechazo `unreachable`, Etapa 5) |
| 3 | Mapa de calles, Dijkstra, reloj simulado, repartidores, flota | DP.1 (ciudad), DP.3 | 6 + 14 | ✅ Hecho |
| 4 | Ventana con el mapa y los puntos en movimiento (OpenCV) | DP.8 | 6 | ✅ Hecho (faltan las ubicaciones de entrega y los contadores finales, que dependen de las etapas 5 y 8) |
| 5 | Despachador: estimaciones simultáneas con `future` y límite de tiempo; recogida y entrega reales; rechazos `unreachable` y `noCourier` | DP.4 (+ DP.2 `unreachable`) | 8 | ✅ Hecho |
| 6 | Cupos de recogida en restaurantes (orden de llegada), capacidad de la bolsa | DP.5 | 8 | ⏳ Pendiente |
| 7 | Averías y transferencia todo-o-nada de pedidos | DP.6 | 8 | ⏳ Pendiente |
| 8 | Registro de eventos JSON y resumen final exacto | DP.7 | 8 | ⏳ Pendiente (ya existe la línea de inicio) |
| 9 | Señales SIGINT/SIGTERM y apagado en menos de 5 s | DP.9 | 6 | ⏳ Pendiente |
| 10 | Análisis con herramientas (ThreadSanitizer, Valgrind) | DP.11 + "Sin condiciones de carrera" | 6 + 8 | 🔶 En curso: TSan ya se usa en cada PR |
| 11 | Experimentos y tabla comparativa | DP.10 | 6 | ⏳ Pendiente |
| 12 | Informe final, diagrama de hilos y canales, mapa de conceptos | DP.12 | 6 | 🔶 En curso |

## Verificaciones (Etapa 3; la Etapa 4 añade el hilo de la ventana)

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
| El mapa solo modela avenidas (troncales, primarias y secundarias): los clientes se ubican sobre ellas, no en calles interiores. | Decisión deliberada para mantener un mapa moderado y legible (138 nodos, 181 calles). | — |
| El resultado final (entregados, rechazados) varía entre ejecuciones con la misma semilla. Ejemplo con el mapa nuevo: 57 / 18 / 24 en Windows y 74 / 3 / 22 en Ubuntu. Los **pedidos generados** sí son idénticos. | Normal en un sistema concurrente: el orden en que el sistema operativo da turno a los hilos cambia los tiempos. El brief exige reproducibilidad en la **generación** (DP.2), y se cumple (test de reproducibilidad). | Explicarlo en el informe; en la Etapa 11 repetir cada experimento varias veces. |
| Una estimación que llega tarde se ignora, pero el `future` de `std::async` espera a que su hilo termine cuando se destruye, al final de `chooseCourier`. | El despachador puede quedar retenido lo que tarde la estimación más lenta (con el estimador real, microsegundos). El resto del sistema no se detiene. | Documentado en DD-024. Si hiciera falta, reemplazar `std::async` por un grupo fijo de hilos con `promise`. |
| Con la ventana abierta en Ubuntu (WSL), ThreadSanitizer reporta 5 avisos **dentro de Qt y dbus**, bibliotecas del sistema que OpenCV usa para la ventana. En los dos accesos de cada aviso, la memoria es de Qt y uno de los hilos lo crea Qt (`QDBusConnection`); ningún dato del proyecto está involucrado. Sin ventana (tests), 0 avisos. | La rúbrica pide 0 reportes de TSan. | Etapa 10: analizarlo a fondo; opciones: archivo de supresiones de TSan limitado a Qt/dbus (documentado y justificado, como pide DP.11) o usar OpenCV con otro backend de ventanas. |
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
| Regenerar el mapa | Los servidores públicos de OpenStreetMap (Overpass) fallaron varias veces seguidas (errores 502/504 y tiempos de espera agotados). El script ahora prueba tres servidores con reintentos y guarda la respuesta en `tools/.cache/` para no repetir la descarga. |

## Mapa (regenerado tras la Etapa 5)

| | Antes | Ahora |
|---|---|---|
| Zona | Entre el 2.º y el 3.er Anillo | Del 2.º al 4.º Anillo, con la Av. San Martín |
| Intersecciones / nodos / calles | 34 / 34 / 40 | 83 intersecciones, 138 nodos (con puntos en curvas) / 181 calles |
| Conectividad | 6 nodos inalcanzables, 3 sumideros | Fuertemente conexo: desde cualquier nodo se llega a cualquier otro |
| Restaurantes alcanzables | 3 de 6 | 6 de 6 |
| Rechazos `unreachable` (3600 s) | 113 o más | 0 |
| Configuración | 8 repartidores, un pedido cada 20 s | 15 repartidores, un pedido por minuto (ver `experiments/00-carga-del-mapa/`) |
