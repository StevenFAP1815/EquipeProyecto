#!/usr/bin/env python3
"""
Verifica la alineacion entre config/equipetrol.json y data/equipetrol.png.

Dibuja nodos (circulos) y calles (lineas) sobre el PNG usando el mismo
GeoBounds::toPixel que usara el runtime en C++ (proyeccion equirectangular).

Salida:
    data/equipetrol_verify.png

Uso:
    pip install Pillow
    python tools/verify_alignment.py
"""

import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

def to_pixel(lat, lon, bounds, width, height):
    """
    Equivalente a GeoBounds::toPixel en C++.
    Proyeccion equirectangular.
    """
    u = (lon - bounds["west"]) / (bounds["east"] - bounds["west"])
    v = (bounds["north"] - lat) / (bounds["north"] - bounds["south"])
    px = int(round(u * (width - 1)))
    py = int(round(v * (height - 1)))
    return px, py


def main():
    repo_root = Path(__file__).resolve().parent.parent
    config_path = repo_root / "config" / "equipetrol.json"
    png_path = repo_root / "data" / "equipetrol.png"
    out_path = repo_root / "data" / "equipetrol_verify.png"

    if not config_path.exists():
        print(f"ERROR: {config_path} no existe.", file=sys.stderr)
        sys.exit(1)
    if not png_path.exists():
        print(f"ERROR: {png_path} no existe.", file=sys.stderr)
        sys.exit(1)

    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)

    bounds = cfg["map"]["bounds"]
    nodes = {n["id"]: (n["lat"], n["lon"]) for n in cfg["nodes"]}
    streets = cfg["streets"]
    restaurants = cfg["restaurants"]

    img = Image.open(png_path).convert("RGB")
    width, height = img.size

    draw = ImageDraw.Draw(img)

    # 1. Dibujar calles (lineas azules finas).
    for s in streets:
        nf = nodes[s["from"]]
        nt = nodes[s["to"]]
        p1 = to_pixel(nf[0], nf[1], bounds, width, height)
        p2 = to_pixel(nt[0], nt[1], bounds, width, height)
        color = (0, 100, 255)  # azul
        draw.line([p1, p2], fill=color, width=2)

    # 2. Dibujar nodos (circulos verdes).
    for nid, (lat, lon) in nodes.items():
        px, py = to_pixel(lat, lon, bounds, width, height)
        r = 5
        draw.ellipse([px - r, py - r, px + r, py + r],
                     fill=(0, 200, 0), outline=(0, 100, 0))

    # 3. Dibujar restaurantes (circulos rojos, mas grandes).
    for r in restaurants:
        nlat, nlon = nodes[r["node"]]
        px, py = to_pixel(nlat, nlon, bounds, width, height)
        rad = 8
        draw.ellipse([px - rad, py - rad, px + rad, py + rad],
                     fill=(255, 50, 50), outline=(150, 0, 0), width=2)

    # 4. Etiqueta pequeña.
    try:
        font = ImageFont.truetype("arial.ttf", 20)
    except Exception:
        font = ImageFont.load_default()
    draw.text((10, 10),
              f"Nodos: {len(nodes)}  Calles: {len(streets)}  "
              f"Restaurantes: {len(restaurants)}",
              fill=(255, 255, 255), font=font)

    img.save(out_path)
    print(f"OK: {out_path}", file=sys.stderr)
    print(f"  Nodos: {len(nodes)}, calles: {len(streets)}, "
          f"restaurantes: {len(restaurants)}", file=sys.stderr)


if __name__ == "__main__":
    main()