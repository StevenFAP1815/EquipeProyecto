import argparse
import json
import statistics


def analyse(log_path):
    created = {}
    latencies = []
    counts = {"created": 0, "assigned": 0, "delivered": 0, "rejected": 0}
    last_t = 0
    with open(log_path, encoding="utf-8") as log:
        for line in log:
            event = json.loads(line)
            t = event["t"]
            last_t = max(last_t, t)
            kind = event["event"]
            if kind == "orderCreated":
                counts["created"] += 1
                created[event["order"]] = t
            elif kind == "orderAssigned":
                counts["assigned"] += 1
                if event["order"] in created:
                    latencies.append(t - created.pop(event["order"]))
            elif kind == "orderDelivered":
                counts["delivered"] += 1
            elif kind == "orderRejected":
                counts["rejected"] += 1
    hours = last_t / 3_600_000 if last_t else float("nan")
    latencies.sort()
    p90 = latencies[int(0.9 * (len(latencies) - 1))] if latencies else float("nan")
    return {
        **counts,
        "simulated_minutes": round(last_t / 60000, 1),
        "assigned_per_sim_hour": round(counts["assigned"] / hours, 1),
        "delivered_per_sim_hour": round(counts["delivered"] / hours, 1),
        "dispatch_latency_median_sim_ms": statistics.median(latencies) if latencies else None,
        "dispatch_latency_p90_sim_ms": p90,
    }


def main():
    parser = argparse.ArgumentParser(description="Effect of the GPU on the running simulation")
    parser.add_argument("logs", nargs="+", help="label=path pairs of event logs")
    args = parser.parse_args()
    rows = []
    for item in args.logs:
        label, path = item.split("=", 1)
        rows.append((label, analyse(path)))
    keys = list(rows[0][1].keys())
    print("| Métrica | " + " | ".join(label for label, _ in rows) + " |")
    print("|---|" + "---|" * len(rows))
    for key in keys:
        print(f"| {key} | " + " | ".join(str(r[key]) for _, r in rows) + " |")


if __name__ == "__main__":
    main()
