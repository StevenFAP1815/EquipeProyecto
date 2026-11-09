import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

METRICS = ["created", "delivered", "rejected", "pending", "avgDeliveryMs", "maxDeliveryMs",
           "reassigned", "breakdowns"]


def set_by_path(config, dotted, value):
    node = config
    keys = dotted.split(".")
    for key in keys[:-1]:
        node = node[key]
    node[keys[-1]] = value


def run_once(binary, config, folder, image):
    folder.mkdir(parents=True, exist_ok=True)
    config = json.loads(json.dumps(config))
    config["map"]["image"] = os.path.relpath(image, folder.resolve()).replace("\\", "/")
    config_path = folder / "config.json"
    config_path.write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")
    log_path = folder / "run.log"
    result = subprocess.run([str(binary), str(config_path), "--log", str(log_path)],
                            capture_output=True, text=True, timeout=600)
    (folder / "stdout.txt").write_text(result.stdout, encoding="utf-8")
    (folder / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0:
        print(f"ERROR en {folder}: código {result.returncode}", file=sys.stderr)
        sys.exit(1)
    return json.loads(result.stdout.strip().splitlines()[-1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--param", required=True)
    parser.add_argument("--values", required=True)
    parser.add_argument("--seeds", default="42,43,44")
    parser.add_argument("--time-scale", type=float, default=600)
    parser.add_argument("--duration", type=int, default=3600)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    base = json.loads(Path(args.base).read_text(encoding="utf-8"))
    base["simulation"]["timeScale"] = args.time_scale
    base["simulation"]["durationS"] = args.duration
    image = (Path(args.base).parent / base["map"]["image"]).resolve()

    out = Path(args.out)
    values = [json.loads(v) for v in args.values.split(",")]
    seeds = [int(s) for s in args.seeds.split(",")]
    rows = []
    for value in values:
        runs = []
        for seed in seeds:
            config = json.loads(json.dumps(base))
            set_by_path(config, args.param, value)
            config["orders"]["seed"] = seed
            summary = run_once(Path(args.binary), config, out / f"{args.param}={value}" / f"seed-{seed}", image)
            runs.append(summary)
            print(f"{args.param}={value} seed={seed}: entregados={summary['delivered']} "
                  f"rechazados={summary['rejected']} promedio={summary['avgDeliveryMs'] / 60000:.1f} min")
        row = {"value": value}
        for metric in METRICS:
            row[metric] = statistics.mean(r[metric] for r in runs)
        rows.append(row)

    lines = [f"| `{args.param}` | Creados | Entregados | Rechazados | Pendientes | "
             f"Entrega promedio (min) | Entrega máxima (min) | Averías | Reasignados |",
             "|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['value']} | {r['created']:.1f} | {r['delivered']:.1f} | {r['rejected']:.1f} | "
                     f"{r['pending']:.1f} | {r['avgDeliveryMs'] / 60000:.1f} | {r['maxDeliveryMs'] / 60000:.1f} | "
                     f"{r['breakdowns']:.1f} | {r['reassigned']:.1f} |")
    table = "\n".join(lines)
    (out / "tabla.md").write_text(table + "\n", encoding="utf-8")
    print(table)


if __name__ == "__main__":
    main()
