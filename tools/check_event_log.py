import json
import sys
from collections import defaultdict

ORDER_EVENTS = {"orderCreated", "orderAssigned", "pickupStarted", "orderPickedUp",
                "orderDelivered", "orderRejected", "orderReassigned"}
ALL_EVENTS = ORDER_EVENTS | {"courierBreakdown", "simulationStopping"}
REQUIRED = {
    "orderCreated": {"order", "restaurant"},
    "orderAssigned": {"order", "courier"},
    "pickupStarted": {"order", "courier", "restaurant"},
    "orderPickedUp": {"order", "courier", "restaurant"},
    "orderDelivered": {"order", "courier"},
    "orderRejected": {"order", "reason"},
    "orderReassigned": {"order", "from", "to"},
    "courierBreakdown": {"courier"},
    "simulationStopping": set(),
}
REASONS = {"queueFull", "unreachable", "noCourier"}


def check(log_path, summary_text):
    errors = []
    counts = defaultdict(int)
    delivered_per_courier = defaultdict(int)
    last_t = {}
    stage = {}
    holder = {}
    final = {}

    with open(log_path, encoding="utf-8") as f:
        for number, line in enumerate(f, start=1):
            try:
                e = json.loads(line)
            except json.JSONDecodeError:
                errors.append(f"línea {number}: no es JSON válido")
                continue
            name = e.get("event")
            if name not in ALL_EVENTS or not isinstance(e.get("t"), int):
                errors.append(f"línea {number}: evento o t inválido")
                continue
            missing = REQUIRED[name] - e.keys()
            if missing:
                errors.append(f"línea {number}: faltan campos {sorted(missing)}")
                continue
            counts[name] += 1
            if name not in ORDER_EVENTS:
                continue

            order = e["order"]
            if order in final:
                errors.append(f"línea {number}: {order} tiene eventos después de su estado final")
            if e["t"] < last_t.get(order, e["t"]):
                errors.append(f"línea {number}: el tiempo de {order} retrocede")
            last_t[order] = e["t"]

            if name == "orderCreated":
                if order in stage:
                    errors.append(f"línea {number}: {order} creado dos veces")
                stage[order] = "created"
            elif order not in stage:
                errors.append(f"línea {number}: {order} sin orderCreated previo")
            elif name == "orderAssigned":
                if order in holder:
                    errors.append(f"línea {number}: {order} asignado dos veces")
                holder[order] = e["courier"]
                stage[order] = "assigned"
            elif name in ("pickupStarted", "orderPickedUp"):
                if holder.get(order) != e["courier"]:
                    errors.append(f"línea {number}: {order} lo recoge quien no lo tiene")
                if name == "orderPickedUp":
                    stage[order] = "pickedUp"
            elif name == "orderReassigned":
                if holder.get(order) != e["from"]:
                    errors.append(f"línea {number}: {order} reasignado desde quien no lo tiene")
                holder[order] = e["to"]
            elif name == "orderDelivered":
                if stage[order] != "pickedUp" or holder.get(order) != e["courier"]:
                    errors.append(f"línea {number}: entrega de {order} fuera de orden o por otro repartidor")
                delivered_per_courier[e["courier"]] += 1
                final[order] = "delivered"
            elif name == "orderRejected":
                if e["reason"] not in REASONS:
                    errors.append(f"línea {number}: motivo inválido")
                final[order] = "rejected"

    if counts["simulationStopping"] != 1:
        errors.append("simulationStopping debe aparecer exactamente una vez")

    summary = json.loads(summary_text)
    expected = {
        "created": counts["orderCreated"],
        "delivered": counts["orderDelivered"],
        "rejected": counts["orderRejected"],
        "pending": counts["orderCreated"] - counts["orderDelivered"] - counts["orderRejected"],
        "reassigned": counts["orderReassigned"],
        "breakdowns": counts["courierBreakdown"],
    }
    for key, value in expected.items():
        if summary.get(key) != value:
            errors.append(f"resumen: {key} = {summary.get(key)}, el registro dice {value}")
    for courier, value in delivered_per_courier.items():
        if summary["deliveredPerCourier"].get(courier) != value:
            errors.append(f"resumen: deliveredPerCourier[{courier}] no coincide")
    if sum(summary["deliveredPerCourier"].values()) != summary["delivered"]:
        errors.append("resumen: deliveredPerCourier no suma delivered")

    return errors, counts


def main():
    if len(sys.argv) != 3:
        print("Uso: python tools/check_event_log.py <events.log> <salida_del_programa.txt>")
        sys.exit(2)
    with open(sys.argv[2], encoding="utf-8") as f:
        lines = [l for l in f.read().splitlines() if l.strip()]
    errors, counts = check(sys.argv[1], lines[-1])
    print(f"Eventos: {dict(counts)}")
    if errors:
        print(f"{len(errors)} problema(s):")
        for e in errors[:20]:
            print("  -", e)
        sys.exit(1)
    print("OK: el registro y el resumen cumplen las reglas de las secciones 4 y 6.")


if __name__ == "__main__":
    main()
