# Experimentos (DP.10)

> Datos crudos de cada ejecución (configuración, registro de eventos y resumen) en [`experiments/`](../experiments/).

## Método (igual para todos)

- **Base:** `config/equipetrol.json`: mapa regenerado (138 nodos, 181 calles, 6 restaurantes), 15 repartidores, bolsa de 3, un pedido por minuto en promedio, ráfagas de hasta 12 pedidos, `maxPending` 50, `acceptTimeoutMs` 600000, `breakdownProbability` 0,1, 3600 s simulados.
- **Una sola variable por experimento:** todo lo demás queda igual a la base.
- **Repeticiones:** cada variante se ejecuta con las semillas **42, 43 y 44**, las mismas para todas las variantes, y la tabla muestra el **promedio**. Con la misma semilla se generan los mismos pedidos, pero el resultado final varía algo entre ejecuciones porque depende de cómo el sistema operativo reparte el tiempo entre los hilos. Ejemplo: la configuración `bagCapacity=3, seed=42` dio 75 entregas y, al repetirla, 70.
- **Aceleración:** `timeScale` 600 (una hora simulada en 6 s reales), igual para todas.
- **Medido:** el resumen final de cada ejecución (sección 6.3).
- **Reproducir un experimento:**
  ```bash
  python tools/run_experiment.py --binary build/delivery_sim --base config/equipetrol.json \
      --param fleet.bagCapacity --values "1,2,3" --out experiments/01-capacidad-de-la-bolsa
  ```
  O reejecutar una configuración guardada: `delivery_sim experiments/<exp>/<variante>/seed-42/config.json`.

---

## Experimento 1: capacidad de la bolsa → **cambio de diseño**

- **Cambiado:** `fleet.bagCapacity` = 1, 2, 3.
- **Fijo:** todo lo demás de la base.
- **Medido:** entregados, rechazados, tiempo de entrega.

| `fleet.bagCapacity` | Creados | Entregados | Rechazados | Pendientes | Entrega promedio (min) | Entrega máxima (min) | Averías | Reasignados |
|---|---|---|---|---|---|---|---|---|
| 1 | 91,0 | 53,0 | 16,7 | 21,3 | 16,4 | 27,8 | 3,0 | 3,0 |
| 2 | 91,3 | 61,0 | 1,3 | 29,0 | 14,0 | 27,9 | 3,0 | 4,0 |
| 3 | 91,0 | 65,0 | 0,0 | 26,0 | 13,8 | 29,7 | 3,0 | 4,0 |

**Conclusión:** con un pedido por viaje, la flota no alcanza y casi un 20 % de los pedidos se rechaza: el libro se llena (`queueFull`) o nadie los toma a tiempo (`noCourier`). Agrupar pedidos del mismo restaurante elimina los rechazos y **baja** la entrega promedio de 16,4 a 13,8 min, porque se evitan viajes repetidos al restaurante.

**Cambio de diseño que motivó (Etapa 6, PR #20):** hasta la Etapa 5 cada repartidor llevaba **un** pedido por viaje (equivale a la fila `bagCapacity = 1`). Al ver la tasa de rechazos con el mapa nuevo se implementó la **bolsa por restaurante** (DD-026). Antes / después:

| | Antes (un pedido por viaje) | Después (bolsa de 3) |
|---|---|---|
| Rechazados | 16,7 | **0,0** |
| Entregados | 53,0 | **65,0** |
| Entrega promedio | 16,4 min | **13,8 min** |

**Experimento previo que motivó otro cambio** ([`experiments/00-carga-del-mapa/`](../experiments/00-carga-del-mapa/resultados.md)): al regenerar el mapa, con 8 repartidores y un pedido cada 20 s se rechazaba el 72 % de los pedidos. Variando repartidores e intervalo se eligió la configuración actual: 15 repartidores y un pedido por minuto.

---

## Experimento 2: cantidad de repartidores

- **Cambiado:** `fleet.couriers` = 5, 8, 15, 25.
- **Fijo:** todo lo demás de la base (bolsa de 3).

| `fleet.couriers` | Creados | Entregados | Rechazados | Pendientes | Entrega promedio (min) | Entrega máxima (min) | Averías | Reasignados |
|---|---|---|---|---|---|---|---|---|
| 5 | 91,0 | 24,3 | 48,7 | 18,0 | 20,9 | 34,1 | 1,3 | 1,7 |
| 8 | 91,3 | 37,7 | 32,3 | 21,3 | 20,3 | 36,6 | 2,3 | 4,0 |
| 15 | 91,3 | 63,0 | 0,0 | 28,3 | 14,1 | 30,6 | 3,0 | 5,0 |
| 25 | 91,3 | 75,7 | 0,0 | 15,7 | 11,4 | 21,3 | 4,0 | 4,3 |

**Conclusión:** por debajo de unos 15 repartidores, la flota es el cuello de botella: más de la mitad de los pedidos se rechaza con 5. A partir de 15 ya no hay rechazos. Pasar a 25 sigue mejorando la **rapidez** (entrega promedio de 14,1 a 11,4 min; máxima de 30,6 a 21,3) porque siempre hay alguien libre cerca, pero con rendimientos decrecientes: +67 % de repartidores da −19 % de tiempo. 15 es un buen equilibrio para la base.

---

## Experimento 3: probabilidad de avería

- **Cambiado:** `incidents.breakdownProbability` = 0; 0,1; 0,3; 1.
- **Fijo:** todo lo demás de la base.

| `breakdownProbability` | Creados | Entregados | Rechazados | Pendientes | Entrega promedio (min) | Entrega máxima (min) | Averías | Reasignados |
|---|---|---|---|---|---|---|---|---|
| 0 | 91,3 | 64,3 | 0,0 | 27,0 | 13,4 | 30,1 | 0,0 | 0,0 |
| 0,1 | 91,0 | 64,0 | 0,7 | 26,3 | 14,2 | 28,9 | 3,0 | 5,3 |
| 0,3 | 91,0 | 54,7 | 3,7 | 32,7 | 16,7 | 30,7 | 14,3 | 21,7 |
| 1 | 91,3 | 43,3 | 17,3 | 30,7 | 21,5 | 37,8 | 35,7 | 43,7 |

**Conclusión:** con 10 % de averías el impacto es casi nulo: los pedidos se transfieren y se entregan. Al crecer la probabilidad, cada avería deja a un repartidor 5 min fuera de servicio y obliga a otro a desviarse. Con probabilidad 1 las entregas caen un tercio y el tiempo promedio sube un 60 %, **pero el sistema nunca se bloquea**: es la garantía de la reparación con límite (DD-027), que se agregó justamente porque con probabilidad 1 el sistema llegaba a quedar detenido.
