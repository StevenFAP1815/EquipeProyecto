import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

RUNS = [
    ("frames_laptop_equipetrol.csv", "Portátil, Equipetrol, sin GPU"),
    ("frames_colab_equipetrol_cpu.csv", "Colab, Equipetrol, --cpu-only"),
    ("frames_colab_equipetrol_measured.csv", "Colab, Equipetrol, measured"),
    ("frames_colab_equipetrol_always.csv", "Colab, Equipetrol, always"),
    ("frames_laptop_city.csv", "Portátil, ciudad, sin GPU"),
    ("frames_colab_city_cpu.csv", "Colab, ciudad, --cpu-only"),
    ("frames_colab_city_measured.csv", "Colab, ciudad, measured"),
    ("frames_colab_city_always.csv", "Colab, ciudad, always"),
    ("frames_colab_equipetrol_heatmap.csv", "Colab, Equipetrol, mapa de calor GPU"),
    ("frames_colab_city_dense_heatmap.csv", "Colab, ciudad, dibujo simplificado + mapa de calor"),
]
FRAME_CEILING_FPS = 1000.0 / 34.0


def read_intervals(path):
    with open(path, newline="") as f:
        return [float(row["interval_ms"]) for row in csv.DictReader(f)]


def main():
    parser = argparse.ArgumentParser(description="Frame rate of the window per run")
    parser.add_argument("--data", required=True, help="folder with the frames_*.csv files")
    parser.add_argument("--out", required=True, help="output PNG")
    args = parser.parse_args()

    labels = []
    fps = []
    intervals = []
    for name, label in RUNS:
        values = read_intervals(Path(args.data) / name)
        labels.append(label)
        fps.append(len(values) / (sum(values) / 1000.0))
        intervals.append(sorted(values))

    fig, (bars, cdf) = plt.subplots(1, 2, figsize=(12, 4.8))
    colors = ["tab:blue" if "Portátil" in l else "tab:orange" for l in labels]
    bars.barh(labels, fps, color=colors)
    bars.axvline(FRAME_CEILING_FPS, color="gray", linestyle="--", label="techo de waitKey(33)")
    for y, value in enumerate(fps):
        bars.text(value + 0.3, y, f"{value:.1f}", va="center", fontsize=8)
    bars.invert_yaxis()
    bars.set_xlabel("FPS medio")
    bars.set_title("Frecuencia de cuadros por ejecución (60 s reales)")
    bars.legend(fontsize=8, loc="lower right")

    for label, values in zip(labels, intervals):
        n = len(values)
        style = "-" if "Equipetrol" in label else "--"
        cdf.plot(values, [(i + 1) / n for i in range(n)], style, label=label)
    cdf.set_xscale("log")
    cdf.set_xlabel("intervalo entre cuadros (ms, log)")
    cdf.set_ylabel("fracción de cuadros")
    cdf.set_title("Distribución del intervalo entre cuadros")
    cdf.grid(True, which="both", alpha=0.3)
    cdf.legend(fontsize=7)

    fig.tight_layout()
    fig.savefig(args.out, dpi=130)
    print("written", args.out)


if __name__ == "__main__":
    main()
