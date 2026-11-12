import json
import sys
from collections import defaultdict

ORDER_EVENTS = {"orderCreated", "orderAssigned", "pickupStarted", "orderPickedUp",
                "orderDelivered", "orderRejected", "orderReassigned"}
COURIER_LIFECYCLE_EVENTS = {"courierJoined", "courierRetired"}
ALL_EVENTS = ORDER_EVENTS | COURIER_LIFECYCLE_EVENTS | {"courierBreakdown", "simulationStopping"}
REQUIRED = {
    "orderCreated": {"order", "restaurant", "urgent"},
    "orderAssigned": {"order", "courier"},
    "pickupStarted": {"order", "courier", "restaurant"},
    "orderPickedUp": {"order", "courier", "restaurant"},
    "orderDelivered": {"order", "courier"},
    "orderRejected": {"order", "reason"},
    "orderReassigned": {"order", "from", "to"},
    "courierBreakdown": {"courier"},
    "courierJoined": {"courier"},
    "courierRetired": {"courier"},
    "simulationStopping": set(),
}
REASONS = {"queueFull", "unreachable", "noCourier"}
REINFORCEMENT_PREFIX = "r"


def check_reinforcement_window(number, name, e, joined, retired, errors):
    if name == "courierJoined":
        courier = e["courier"]
        if courier in joined:
            errors.append(f"línea {number}: id de refuerzo {courier} reutilizado")
        joined.add(courier)
        return
    if name == "courierRetired":
        courier = e["courier"]
        if courier not in joined:
            errors.append(f"línea {number}: {courier} se retira sin courierJoined previo")
        elif courier in retired:
            errors.append(f"línea {number}: {courier} se retira dos veces")
        retired.add(courier)
        return
    for field in ("courier", "from", "to"):
        courier = e.get(field)
        if not isinstance(courier, str) or not courier.startswith(REINFORCEMENT_PREFIX):
            continue
        if courier not in joined:
            errors.append(f"línea {number}: evento de {courier} antes de courierJoined")
        elif courier in retired:
            errors.append(f"línea {number}: evento de {courier} después de courierRetired")


def check(log_path, summary_text):
    errors = []
    counts = defaultdict(int)
    delivered_per_courier = defaultdict(int)
    last_t = {}
    stage = {}
    holder = {}
    final = {}
    joined = set()
    retired = set()
    created_t = {}
    urgent = set()
    urgent_times = []

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
            check_reinforcement_window(number, name, e, joined, retired, errors)
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
                if not isinstance(e["urgent"], bool):
                    errors.append(f"línea {number}: urgent debe ser true o false")
                elif e["urgent"]:
                    urgent.add(order)
                created_t[order] = e["t"]
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
                if order in urgent:
                    urgent_times.append(e["t"] - created_t[order])
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
        "reinforcementsJoined": counts["courierJoined"],
        "reinforcementsRetired": counts["courierRetired"],
        "urgentCreated": len(urgent),
        "urgentDelivered": len(urgent_times),
        "avgUrgentDeliveryMs": sum(urgent_times) // len(urgent_times) if urgent_times else 0,
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
