import argparse
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HISTOGRAM_LINE = "Delivery-time histogram"
PERCENTILE_LINE = "Delivery-time percentiles"


def parse(path):
    bins = {}
    percentiles = {}
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            if line.startswith(HISTOGRAM_LINE):
                for minute, count in re.findall(r"(\d+)\+?m=(\d+)", line):
                    bins[int(minute)] = int(count)
            elif line.startswith(PERCENTILE_LINE):
                for name, ms in re.findall(r"(p\d+)=(\d+) ms", line):
                    percentiles[name] = int(ms) / 60000.0
    if not bins:
        raise SystemExit(f"no histogram line in {path}")
    return bins, percentiles


def main():
    parser = argparse.ArgumentParser(description="Plot the delivery-time histogram printed by the simulator")
    parser.add_argument("--input", required=True, help="simulator output with the histogram line")
    parser.add_argument("--out", required=True, help="output PNG")
    parser.add_argument("--title", default="Histograma de tiempos de entrega")
    args = parser.parse_args()

    bins, percentiles = parse(args.input)
    minutes = sorted(bins)
    counts = [bins[m] for m in minutes]

    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.bar(minutes, counts, width=0.9, color="tab:blue", label=f"{sum(counts)} entregas")
    for (name, value), color in zip(sorted(percentiles.items()), ["tab:green", "tab:orange", "tab:red"]):
        ax.axvline(value, color=color, linestyle="--", label=f"{name} = {value:.1f} min")
    ax.set_xlabel("tiempo de entrega (minutos, intervalos de 1 minuto)")
    ax.set_ylabel("entregas")
    ax.set_title(args.title)
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(args.out, dpi=130)
    print("written", args.out)


if __name__ == "__main__":
    main()
