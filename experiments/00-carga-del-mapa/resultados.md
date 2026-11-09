# Experimento preliminar 00: carga de trabajo con el mapa nuevo

> Preliminar para DP.10. Se repetirá en la Etapa 11 con registro de eventos y varias semillas.

- **Fecha:** 09/11/2026.
- **Código:** rama `regenerar-mapa-v2` (despachador con estimaciones, sin bolsa múltiple).
- **Fijo:** `config/equipetrol.json` regenerado (138 nodos, 181 calles, 6 restaurantes), `speedKmh` 30, `seed` 42, `durationS` 3600, `timeScale` 600, `maxPending` 50, `burstMax` 12, `acceptTimeoutMs` 600000.
- **Variable:** `fleet.couriers` y `orders.meanIntervalMs`.
- **Medido:** resumen final (`created`, `delivered`, `rejected`, `pending`).

| Repartidores | `meanIntervalMs` | Creados | Entregados | Rechazados | Pendientes | % rechazados |
|---|---|---|---|---|---|---|
| 8 | 20000 | 239 | 29 | 173 | 37 | 72 % |
| 8 | 45000 | 133 | 32 | 68 | 33 | 51 % |
| 10 | 45000 | 133 | 40 | 58 | 35 | 44 % |
| 10 | 60000 | 99 | 40 | 42 | 17 | 42 % |
| 12 | 45000 | 133 | 46 | 50 | 37 | 38 % |
| 12 | 60000 | 99 | 48 | 31 | 20 | 31 % |
| 15 | 60000 | 99 | 58 | 18 | 23 | 18 % |

**Conclusión:** cada repartidor completa unas 4 entregas por hora simulada en este mapa: unos 3 km por viaje a 30 km/h, más la espera de la comida. Con 8 repartidores y un pedido cada 20 s, la demanda (unos 180 pedidos por hora) supera muchas veces la capacidad (unos 35 por hora), y el libro se llena (`queueFull`). **Cambio de diseño:** la configuración pasa a 15 repartidores y un pedido por minuto.
