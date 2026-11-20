import argparse
import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

T4_PEAK_OPS = 8.1e12
T4_PEAK_BYTES = 320e9

ROOFLINE_MODELS = {
    "travel_table": ("FW por bloques (fase 3)", lambda n: 2.0 * n ** 3,
                     lambda n: (n / 32) ** 3 * 16384.0),
    "eta_batch": ("ETA SoA", lambda p: 20.0 * p, lambda p: 5 * 32.0 * p + 8.0 * p),
    "dispatch_argmin": ("argmin por fila", lambda e: 1.0 * e, lambda e: 8.0 * e),
    "candidate_filter": ("filtro estable", lambda n: 12.0 * n, lambda n: 22.0 * n),
    "delivery_histogram": ("histograma privatizado", lambda n: 3.0 * n, lambda n: 8.0 * n),
    "delivery_percentiles": ("radix sort 1 bit", lambda n: 22 * 10.0 * n, lambda n: 22 * 12.0 * n),
    "orders_by_restaurant": ("agrupación estable", lambda n: 140.0 * n, lambda n: 16.0 * n),
    "courier_log_merge": ("mezcla por rondas", lambda n: 12 * 6.0 * n, lambda n: 12 * 24.0 * n),
    "demand_heatmap": ("convolución 5x5", lambda c: 50.0 * c, lambda c: 10.25 * c),
    "coverage_field": ("Jacobi 5 puntos x100", lambda c: 100 * 5.0 * c,
                       lambda c: 100 * 13.1 * c),
    "response_map": ("min-plus 5 puntos x100", lambda c: 100 * 6.0 * c,
                     lambda c: 100 * 13.1 * c),
    "route_arrivals": ("scan segmentado (3 kernels)", lambda n: 30.0 * n, lambda n: 23.0 * n),
}
CHAINED_APPS = ["response_histogram"]


def size_value(label):
    parts = [int(p) for p in re.findall(r"\d+", str(label))]
    value = 1
    for p in parts:
        value *= p
    return value


def summarize(df):
    grouped = df.groupby(["application", "size", "version"])["ms"]
    summary = grouped.agg(median="median", low="min", high="max").reset_index()
    correct = df.groupby(["application", "size", "version"])["correct"].apply(
        lambda c: (c == "yes").all()).reset_index()
    summary = summary.merge(correct, on=["application", "size", "version"])
    summary["n"] = summary["size"].map(size_value)
    return summary


def best_cpu(summary, app):
    cpu = summary[(summary.application == app) & summary.version.str.startswith("cpu_t")]
    idx = cpu.groupby("size")["median"].idxmin()
    best = cpu.loc[idx, ["size", "version", "median", "low", "high"]]
    best["threads"] = best.version.str.replace("cpu_t", "").astype(int)
    return best.set_index("size")


def value(summary, app, size, version, column="median"):
    row = summary[(summary.application == app) & (summary["size"] == size) &
                  (summary.version == version)]
    return float(row[column].iloc[0]) if len(row) else float("nan")


def format_ms(x):
    if pd.isna(x):
        return "—"
    return f"{x:.3f}" if x < 10 else f"{x:.1f}"


def app_table(laptop, colab, app):
    sizes = sorted(colab[colab.application == app]["size"].unique(), key=size_value)
    best = best_cpu(laptop, app)
    lines = ["| Tamaño | CPU 1 hilo (ms) | CPU N hilos (ms) | GPU kernel (ms) | GPU e2e (ms) "
             "| Speedup vs mejor CPU (kernel / e2e) | ¿Correcto? |",
             "|---|---|---|---|---|---|---|"]
    crossover = None
    for size in sizes:
        cpu1 = value(laptop, app, size, "cpu_t1")
        bcpu = best.loc[size, "median"] if size in best.index else float("nan")
        threads = int(best.loc[size, "threads"]) if size in best.index else 0
        kernel = value(colab, app, size, "gpu_kernel")
        e2e = value(colab, app, size, "gpu_e2e")
        ok = all(bool(r) for r in pd.concat([
            laptop[(laptop.application == app) & (laptop["size"] == size)].correct,
            colab[(colab.application == app) & (colab["size"] == size)].correct]))
        if crossover is None and e2e < bcpu:
            crossover = size
        lines.append(f"| {size} | {format_ms(cpu1)} | {format_ms(bcpu)} (N={threads}) | "
                     f"{format_ms(kernel)} | {format_ms(e2e)} | "
                     f"{bcpu / kernel:.1f}x / {bcpu / e2e:.2f}x | {'sí' if ok else 'NO'} |")
    return "\n".join(lines), crossover


def colab_table(colab, app):
    sizes = sorted(colab[colab.application == app]["size"].unique(), key=size_value)
    versions = ["cpu_t1", "cpu_t2", "gpu_kernel", "gpu_e2e"]
    lines = ["| Tamaño | " + " | ".join(versions) + " |",
             "|---|" + "---|" * len(versions)]
    for size in sizes:
        cells = [format_ms(value(colab, app, size, v)) for v in versions]
        lines.append(f"| {size} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def plot_times(laptop, colab, app, crossover, out):
    sizes = sorted(colab[colab.application == app]["size"].unique(), key=size_value)
    x = [size_value(s) for s in sizes]
    best = best_cpu(laptop, app)
    series = {
        "CPU 1 hilo (portátil)": [value(laptop, app, s, "cpu_t1") for s in sizes],
        "Mejor CPU multihilo (portátil)": [best.loc[s, "median"] for s in sizes],
        "GPU solo kernel (T4)": [value(colab, app, s, "gpu_kernel") for s in sizes],
        "GPU extremo a extremo (T4)": [value(colab, app, s, "gpu_e2e") for s in sizes],
    }
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for label, ys in series.items():
        ax.plot(x, ys, marker="o", label=label)
    if crossover is not None:
        ax.axvline(size_value(crossover), color="grey", linestyle="--",
                   label=f"cruce: {crossover}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("tamaño del problema (elementos)")
    ax.set_ylabel("tiempo (ms, mediana)")
    ax.set_title(app)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out / f"{app}_time.png", dpi=130)
    plt.close(fig)


def plot_copy_share(colab, app, out):
    sizes = sorted(colab[colab.application == app]["size"].unique(), key=size_value)
    kernel = [value(colab, app, s, "gpu_kernel") for s in sizes]
    e2e = [value(colab, app, s, "gpu_e2e") for s in sizes]
    kernel_share = [k / e if e > 0 else 0 for k, e in zip(kernel, e2e)]
    copy_share = [max(0.0, 1 - k) for k in kernel_share]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    labels = [str(s) for s in sizes]
    ax.bar(labels, kernel_share, label="kernel")
    ax.bar(labels, copy_share, bottom=kernel_share, label="copias, reserva y lanzamiento")
    ax.set_ylim(0, 1)
    ax.set_ylabel("fracción del tiempo GPU e2e")
    ax.set_title(f"{app}: kernel frente a copias")
    ax.legend(fontsize=8)
    plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    fig.tight_layout()
    fig.savefig(out / f"{app}_copy_share.png", dpi=130)
    plt.close(fig)


def plot_thread_sweep(laptop, out):
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for app in sorted(laptop.application.unique()):
        sub = laptop[(laptop.application == app) & laptop.version.str.startswith("cpu_t")]
        largest = max(sub["size"].unique(), key=size_value)
        rows = sub[sub["size"] == largest].copy()
        rows["threads"] = rows.version.str.replace("cpu_t", "").astype(int)
        rows = rows.sort_values("threads")
        base = float(rows[rows.threads == 1]["median"].iloc[0])
        ax.plot(rows.threads, base / rows["median"], marker="o", label=f"{app} ({largest})")
    ax.plot([1, 8], [1, 8], color="grey", linestyle=":", label="ideal")
    ax.set_xlabel("hilos")
    ax.set_ylabel("speedup frente a 1 hilo")
    ax.set_title("Barrido de hilos en el portátil (tamaño mayor)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=6)
    fig.tight_layout()
    fig.savefig(out / "thread_sweep.png", dpi=130)
    plt.close(fig)


def roofline(colab, out):
    rows = []
    fig, ax = plt.subplots(figsize=(6.4, 4.6))
    intensities = [2 ** k for k in range(-5, 8)]
    ax.plot(intensities, [min(T4_PEAK_OPS, i * T4_PEAK_BYTES) for i in intensities],
            color="black", label="techo T4 (320 GB/s, 8,1 Tops/s)")
    for app, (name, ops_of, bytes_of) in ROOFLINE_MODELS.items():
        sub = colab[(colab.application == app) & (colab.version == "gpu_kernel")]
        if sub.empty:
            continue
        largest = max(sub["size"].unique(), key=size_value)
        n = size_value(largest)
        seconds = value(colab, app, largest, "gpu_kernel") / 1000.0
        ops = ops_of(n)
        traffic = bytes_of(n)
        intensity = ops / traffic
        achieved = ops / seconds
        rows.append((app, name, largest, intensity, achieved / 1e9))
        ax.scatter(intensity, achieved)
        ax.annotate(name, (intensity, achieved), fontsize=6)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("intensidad aritmética (operaciones / byte de memoria global)")
    ax.set_ylabel("rendimiento alcanzado (operaciones / s)")
    ax.set_title("Roofline de los kernels principales (T4)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(out / "roofline.png", dpi=130)
    plt.close(fig)
    lines = ["| Aplicación | Kernel | Tamaño | Intensidad (op/B) | Alcanzado (Gop/s) |",
             "|---|---|---|---|---|"]
    for app, name, size, intensity, gops in rows:
        lines.append(f"| {app} | {name} | {size} | {intensity:.2f} | {gops:.1f} |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Tables and plots of the GPU benchmark")
    parser.add_argument("--laptop", type=Path, required=True)
    parser.add_argument("--colab", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    laptop = summarize(pd.read_csv(args.laptop))
    colab = summarize(pd.read_csv(args.colab))
    laptop.to_csv(args.out / "laptop_summary.csv", index=False)
    colab.to_csv(args.out / "colab_summary.csv", index=False)

    sections = []
    for app in list(ROOFLINE_MODELS) + CHAINED_APPS:
        if app not in set(colab.application):
            continue
        table, crossover = app_table(laptop, colab, app)
        plot_times(laptop, colab, app, crossover, args.out)
        plot_copy_share(colab, app, args.out)
        sections.append(f"### {app}\n\nCruce (GPU e2e < mejor CPU del portátil): "
                        f"{crossover if crossover else 'no se alcanza'}\n\n{table}\n\n"
                        f"Misma máquina (Colab):\n\n{colab_table(colab, app)}\n")
    plot_thread_sweep(laptop, args.out)
    sections.append("### Roofline\n\n" + roofline(colab, args.out) + "\n")
    (args.out / "tables.md").write_text("\n".join(sections), encoding="utf-8")
    print(f"written {args.out / 'tables.md'} and figures")


if __name__ == "__main__":
    main()
