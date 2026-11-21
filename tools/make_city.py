import argparse
import json
import random
from pathlib import Path

CENTER_LAT = -17.7800
CENTER_LON = -63.1800
STEP_DEG = 0.00125
JITTER = 0.25
AVENUE_EVERY = 4


def node_id(rows, cols, r, c):
    return f"n{r * cols + c}"


def build_nodes(rng, rows, cols):
    nodes = []
    lat0 = CENTER_LAT + STEP_DEG * (rows - 1) / 2
    lon0 = CENTER_LON - STEP_DEG * (cols - 1) / 2
    for r in range(rows):
        for c in range(cols):
            lat = lat0 - r * STEP_DEG + rng.uniform(-JITTER, JITTER) * STEP_DEG
            lon = lon0 + c * STEP_DEG + rng.uniform(-JITTER, JITTER) * STEP_DEG
            nodes.append({"id": node_id(rows, cols, r, c),
                          "lat": round(lat, 7), "lon": round(lon, 7)})
    return nodes


def build_streets(rows, cols):
    streets = []

    def add(a, b, one_way):
        streets.append({"id": f"s{len(streets)}", "from": a, "to": b, "oneWay": one_way})

    for r in range(rows):
        avenue = r % AVENUE_EVERY == 0
        eastbound = r % 2 == 0
        for c in range(cols - 1):
            west = node_id(rows, cols, r, c)
            east = node_id(rows, cols, r, c + 1)
            if avenue or eastbound:
                add(west, east, not avenue)
            else:
                add(east, west, True)
    for c in range(cols):
        for r in range(rows - 1):
            add(node_id(rows, cols, r, c), node_id(rows, cols, r + 1, c), False)
    return streets


def build_restaurants(rng, rows, cols, count):
    chosen = rng.sample(range(rows * cols), count)
    restaurants = []
    for i, index in enumerate(chosen):
        restaurants.append({
            "id": f"r{i}",
            "name": f"Restaurant {i}",
            "node": f"n{index}",
            "pickupSlots": rng.randint(1, 3),
            "prepTimeMs": [60000, 240000],
        })
    return restaurants


def bounds(nodes):
    margin = STEP_DEG
    lats = [n["lat"] for n in nodes]
    lons = [n["lon"] for n in nodes]
    return {"north": max(lats) + margin, "south": min(lats) - margin,
            "west": min(lons) - margin, "east": max(lons) + margin}


def make_city(rows, cols, couriers, restaurants, seed):
    rng = random.Random(seed)
    nodes = build_nodes(rng, rows, cols)
    return {
        "map": {"image": "../data/equipetrol.png",
                "attribution": "Synthetic street grid",
                "bounds": bounds(nodes)},
        "nodes": nodes,
        "streets": build_streets(rows, cols),
        "restaurants": build_restaurants(rng, rows, cols, restaurants),
        "fleet": {"couriers": couriers, "bagCapacity": 3, "speedKmh": 30,
                  "startNode": node_id(rows, cols, rows // 2, cols // 2)},
        "orders": {"meanIntervalMs": 300, "burstMax": 20, "maxPending": 1000, "seed": seed},
        "dispatch": {"quoteTimeoutMs": 200, "acceptTimeoutMs": 600000},
        "incidents": {"breakdownProbability": 0.01},
        "simulation": {"durationS": 3600, "timeScale": 60},
    }


def main():
    parser = argparse.ArgumentParser(description="Generate a synthetic city configuration.")
    parser.add_argument("--rows", type=int, default=64)
    parser.add_argument("--cols", type=int, default=64)
    parser.add_argument("--couriers", type=int, default=3000)
    parser.add_argument("--restaurants", type=int, default=60)
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--out", type=Path, default=Path("config/city.json"))
    args = parser.parse_args()

    city = make_city(args.rows, args.cols, args.couriers, args.restaurants, args.seed)
    args.out.write_text(json.dumps(city, indent=1) + "\n", encoding="utf-8")
    print(f"{args.out}: {len(city['nodes'])} nodes, {len(city['streets'])} streets, "
          f"{len(city['restaurants'])} restaurants, {args.couriers} couriers")


if __name__ == "__main__":
    main()
