import json
import math
import sys
import time
from collections import defaultdict, deque
from pathlib import Path

import requests
from staticmap import StaticMap

IMAGE_WIDTH = 2560
IMAGE_HEIGHT = 1706

OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]
USER_AGENT = "EquipetrolDelivery-StudentProject/1.0"

HIGHWAY_FILTER = ["trunk", "primary", "secondary", "tertiary", "unclassified"]
KEEP_HIGHWAYS = ["trunk", "primary", "secondary"]
AMENITY_FILTER = ["restaurant", "fast_food", "cafe"]
KEEP_AMENITIES = ["restaurant", "fast_food"]
EXCLUDED_NAMES = ["Constructora"]

MIN_INTERSECTIONS = 25
MIN_SEGMENTS = 40
NUM_RESTAURANTS = 6
CURVE_TOLERANCE_M = 12.0

EARTH_RADIUS_M = 6371000.0


def haversine(lat1, lon1, lat2, lon2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    a = (math.sin(dphi / 2) ** 2
         + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2)
    return EARTH_RADIUS_M * 2 * math.asin(min(1.0, math.sqrt(a)))


def point_in_polygon(lat, lon, polygon):
    inside = False
    j = len(polygon) - 1
    for i in range(len(polygon)):
        xi, yi = polygon[i]
        xj, yj = polygon[j]
        if ((yi > lat) != (yj > lat)) and \
           (lon < (xj - xi) * (lat - yi) / (yj - yi + 1e-12) + xi):
            inside = not inside
        j = i
    return inside


def load_polygon(path):
    with open(path, encoding="utf-8") as f:
        gj = json.load(f)
    if gj.get("type") == "FeatureCollection":
        coords = gj["features"][0]["geometry"]["coordinates"][0]
    elif gj.get("type") == "Feature":
        coords = gj["geometry"]["coordinates"][0]
    else:
        coords = gj["coordinates"][0]
    return [(lon, lat) for lon, lat in coords]


def polygon_bbox(polygon):
    lons = [p[0] for p in polygon]
    lats = [p[1] for p in polygon]
    return {"north": max(lats), "south": min(lats),
            "west": min(lons), "east": max(lons)}


def mercator_y(lat_deg):
    lat = math.radians(lat_deg)
    return (1 - math.log(math.tan(lat) + 1 / math.cos(lat)) / math.pi) / 2


def calculate_zoom_for_bbox(bbox, width, height):
    dx = (bbox["east"] - bbox["west"]) / 360
    dy = mercator_y(bbox["south"]) - mercator_y(bbox["north"])
    zoom_x = math.log2(width / (dx * 256))
    zoom_y = math.log2(height / (dy * 256))
    return max(1, min(19, int(min(zoom_x, zoom_y))))


def pixel_to_latlon(px, py, zoom, center_lat, center_lon, width, height):
    scale = 256.0 * (2 ** zoom)
    world_x = (center_lon + 180.0) / 360.0 * scale + (px - width / 2.0)
    world_y = mercator_y(center_lat) * scale + (py - height / 2.0)
    lon = world_x / scale * 360.0 - 180.0
    n = math.pi - 2.0 * math.pi * world_y / scale
    return math.degrees(math.atan(math.sinh(n))), lon


def fetch_osm_data(bbox, cache_path):
    if cache_path.exists():
        print(f"[1/8] Usando datos guardados en {cache_path}", file=sys.stderr)
        return json.loads(cache_path.read_text(encoding="utf-8"))

    box = f'{bbox["south"]},{bbox["west"]},{bbox["north"]},{bbox["east"]}'
    query = f"""
    [out:json][timeout:120];
    (
      way["highway"~"^({"|".join(HIGHWAY_FILTER)})$"]({box});
      node["amenity"~"^({"|".join(AMENITY_FILTER)})$"]({box});
    );
    out body;
    >;
    out skel qt;
    """
    for attempt in range(3):
        for url in OVERPASS_URLS:
            print(f"[1/8] Consultando {url} (intento {attempt + 1})", file=sys.stderr)
            try:
                r = requests.post(url, data={"data": query},
                                  headers={"User-Agent": USER_AGENT}, timeout=150)
                r.raise_for_status()
                data = r.json()
                cache_path.parent.mkdir(parents=True, exist_ok=True)
                cache_path.write_text(json.dumps(data), encoding="utf-8")
                return data
            except (requests.RequestException, ValueError) as e:
                print(f"      falló: {e}", file=sys.stderr)
        time.sleep(20)
    print("ERROR: ningún servidor Overpass respondió.", file=sys.stderr)
    sys.exit(1)


def is_one_way(tags):
    if tags.get("junction") in ("roundabout", "circular"):
        return True, False
    value = tags.get("oneway", "no").lower()
    if value == "-1":
        return True, True
    return value in ("yes", "true", "1"), False


def parse_osm(osm):
    nodes = {}
    ways = []
    amenities = []
    for el in osm.get("elements", []):
        if el.get("type") == "node" and "lat" in el:
            nodes[el["id"]] = (el["lat"], el["lon"])
            tags = el.get("tags", {})
            if (tags.get("amenity") in KEEP_AMENITIES and "name" in tags
                    and not any(word in tags["name"] for word in EXCLUDED_NAMES)):
                amenities.append({"lat": el["lat"], "lon": el["lon"],
                                  "name": tags["name"]})
        elif el.get("tags", {}).get("highway") in KEEP_HIGHWAYS:
            one_way, reversed_way = is_one_way(el["tags"])
            path = list(el.get("nodes", []))
            if reversed_way:
                path.reverse()
            ways.append({"path": path, "one_way": one_way})
    print(f"[2/8] {len(nodes)} nodos OSM, {len(ways)} vías, {len(amenities)} locales.",
          file=sys.stderr)
    return nodes, ways, amenities


def split_into_segments(nodes, ways, polygon):
    neighbors = defaultdict(set)
    allowed = set()
    for w in ways:
        path = [n for n in w["path"] if n in nodes]
        for a, b in zip(path, path[1:]):
            neighbors[a].add(b)
            neighbors[b].add(a)
            allowed.add((a, b))
            if not w["one_way"]:
                allowed.add((b, a))

    junctions = {nid for nid, near in neighbors.items()
                 if len(near) >= 3 and point_in_polygon(*nodes[nid], polygon)}

    found = {}
    for start in junctions:
        for first in neighbors[start]:
            path = [start, first]
            while path[-1] not in junctions and len(neighbors[path[-1]]) == 2 and len(path) < 5000:
                previous, current = path[-2], path[-1]
                path.append(next(n for n in neighbors[current] if n != previous))
            end = path[-1]
            if end not in junctions or end == start:
                continue
            forward = all((a, b) in allowed for a, b in zip(path, path[1:]))
            backward = all((b, a) in allowed for a, b in zip(path, path[1:]))
            if not forward and not backward:
                continue
            key = tuple(path) if path[0] < path[-1] else tuple(reversed(path))
            if key in found:
                continue
            if forward and backward:
                found[key] = {"from": key[0], "to": key[-1], "one_way": False, "path": list(key)}
            elif forward:
                found[key] = {"from": start, "to": end, "one_way": True, "path": path}
            else:
                found[key] = {"from": end, "to": start, "one_way": True,
                              "path": list(reversed(path))}

    segments = list(found.values())
    print(f"[3/8] {len(junctions)} intersecciones, {len(segments)} tramos.", file=sys.stderr)
    return junctions, segments


def largest_strongly_connected(junctions, segments):
    forward = defaultdict(set)
    backward = defaultdict(set)
    for s in segments:
        forward[s["from"]].add(s["to"])
        backward[s["to"]].add(s["from"])
        if not s["one_way"]:
            forward[s["to"]].add(s["from"])
            backward[s["from"]].add(s["to"])

    def reach(start, graph):
        seen = {start}
        queue = deque([start])
        while queue:
            u = queue.popleft()
            for v in graph[u]:
                if v not in seen:
                    seen.add(v)
                    queue.append(v)
        return seen

    best = set()
    assigned = set()
    for node in junctions:
        if node in assigned:
            continue
        component = reach(node, forward) & reach(node, backward)
        assigned |= component
        if len(component) > len(best):
            best = component

    kept = [s for s in segments if s["from"] in best and s["to"] in best]
    print(f"[4/8] Componente fuertemente conexa: {len(best)} intersecciones, "
          f"{len(kept)} tramos.", file=sys.stderr)
    return best, kept


def simplify(path, nodes):
    if len(path) <= 2:
        return [path[0], path[-1]]
    a, b = nodes[path[0]], nodes[path[-1]]
    worst, worst_i = 0.0, 0
    for i in range(1, len(path) - 1):
        p = nodes[path[i]]
        d = distance_to_line(p, a, b)
        if d > worst:
            worst, worst_i = d, i
    if worst <= CURVE_TOLERANCE_M:
        return [path[0], path[-1]]
    left = simplify(path[:worst_i + 1], nodes)
    right = simplify(path[worst_i:], nodes)
    return left[:-1] + right


def distance_to_line(p, a, b):
    lat0 = math.radians(a[0])
    def xy(q):
        return ((q[1] - a[1]) * 111320 * math.cos(lat0), (q[0] - a[0]) * 110540)
    px, py = xy(p)
    bx, by = xy(b)
    length2 = bx * bx + by * by
    if length2 == 0:
        return math.hypot(px, py)
    t = max(0.0, min(1.0, (px * bx + py * by) / length2))
    return math.hypot(px - t * bx, py - t * by)


def build_final_graph(nodes, segments):
    graph_nodes = set()
    edges = []
    for s in segments:
        points = simplify(s["path"], nodes)
        graph_nodes.update(points)
        for u, v in zip(points, points[1:]):
            edges.append((u, v, s["one_way"]))
    print(f"[5/8] Con puntos en las curvas: {len(graph_nodes)} nodos, {len(edges)} calles.",
          file=sys.stderr)
    return graph_nodes, edges


def pick_restaurants(amenities, junctions, nodes):
    if len(amenities) < NUM_RESTAURANTS:
        print("ERROR: no hay suficientes restaurantes.", file=sys.stderr)
        sys.exit(1)

    def nearest_junction(lat, lon):
        return min(junctions, key=lambda nid: haversine(lat, lon, *nodes[nid]))

    candidates = [a for a in amenities
                  if haversine(a["lat"], a["lon"], *nodes[nearest_junction(a["lat"], a["lon"])]) < 150]
    clat = sum(nodes[n][0] for n in junctions) / len(junctions)
    clon = sum(nodes[n][1] for n in junctions) / len(junctions)
    candidates.sort(key=lambda a: haversine(a["lat"], a["lon"], clat, clon))

    chosen = [candidates[0]]
    used_nodes = {nearest_junction(candidates[0]["lat"], candidates[0]["lon"])}
    while len(chosen) < NUM_RESTAURANTS:
        remaining = [a for a in candidates
                     if a not in chosen and nearest_junction(a["lat"], a["lon"]) not in used_nodes]
        if not remaining:
            print("ERROR: no hay restaurantes en intersecciones distintas.", file=sys.stderr)
            sys.exit(1)
        far = max(remaining, key=lambda a: min(
            haversine(a["lat"], a["lon"], c["lat"], c["lon"]) for c in chosen))
        chosen.append(far)
        used_nodes.add(nearest_junction(far["lat"], far["lon"]))

    for r in chosen:
        r["node"] = nearest_junction(r["lat"], r["lon"])
    print(f"[6/8] Restaurantes: {', '.join(r['name'] for r in chosen)}", file=sys.stderr)
    return chosen


def generate_png(output_path, bbox):
    zoom = calculate_zoom_for_bbox(bbox, IMAGE_WIDTH, IMAGE_HEIGHT)
    clat = (bbox["north"] + bbox["south"]) / 2
    clon = (bbox["west"] + bbox["east"]) / 2
    print(f"[7/8] Imagen {IMAGE_WIDTH}x{IMAGE_HEIGHT}, zoom {zoom}", file=sys.stderr)
    m = StaticMap(IMAGE_WIDTH, IMAGE_HEIGHT,
                  url_template="https://tile.openstreetmap.org/{z}/{x}/{y}.png",
                  headers={"User-Agent": USER_AGENT})
    m.render(zoom=zoom, center=[clon, clat]).save(str(output_path))
    nw = pixel_to_latlon(0, 0, zoom, clat, clon, IMAGE_WIDTH, IMAGE_HEIGHT)
    se = pixel_to_latlon(IMAGE_WIDTH, IMAGE_HEIGHT, zoom, clat, clon, IMAGE_WIDTH, IMAGE_HEIGHT)
    return {"north": nw[0], "south": se[0], "west": nw[1], "east": se[1]}


def emit_json(nodes, graph_nodes, edges, junctions, restaurants, bounds, output_path):
    ids = {}
    nodes_out = []
    for i, osm_id in enumerate(sorted(graph_nodes)):
        ids[osm_id] = f"n{i}"
        lat, lon = nodes[osm_id]
        nodes_out.append({"id": f"n{i}", "lat": lat, "lon": lon})

    degree = defaultdict(int)
    for u, v, _ in edges:
        degree[u] += 1
        degree[v] += 1
    start = max(junctions, key=lambda n: degree[n])

    config = {
        "map": {
            "image": "../data/equipetrol.png",
            "attribution": "(c) OpenStreetMap contributors",
            "bounds": bounds,
        },
        "nodes": nodes_out,
        "streets": [{"id": f"s{i}", "from": ids[u], "to": ids[v], "oneWay": one_way}
                    for i, (u, v, one_way) in enumerate(edges)],
        "restaurants": [{"id": f"r{i}", "name": r["name"], "node": ids[r["node"]],
                         "pickupSlots": 1, "prepTimeMs": [60000, 240000]}
                        for i, r in enumerate(restaurants)],
        "fleet": {"couriers": 15, "bagCapacity": 3, "speedKmh": 30, "startNode": ids[start]},
        "orders": {"meanIntervalMs": 60000, "burstMax": 12, "maxPending": 50, "seed": 42},
        "dispatch": {"quoteTimeoutMs": 200, "acceptTimeoutMs": 600000},
        "incidents": {"breakdownProbability": 0.1},
        "simulation": {"durationS": 3600, "timeScale": 60},
    }
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)
    print(f"[8/8] {output_path}: {len(nodes_out)} nodos, {len(edges)} calles, "
          f"{len(restaurants)} restaurantes.", file=sys.stderr)


def main():
    root = Path(__file__).resolve().parent.parent
    polygon = load_polygon(root / "data" / "area.geojson")
    bbox = polygon_bbox(polygon)

    osm = fetch_osm_data(bbox, root / "tools" / ".cache" / "osm.json")
    nodes, ways, amenities = parse_osm(osm)
    junctions, segments = split_into_segments(nodes, ways, polygon)
    junctions, segments = largest_strongly_connected(junctions, segments)

    if len(junctions) < MIN_INTERSECTIONS or len(segments) < MIN_SEGMENTS:
        print(f"ERROR: mapa demasiado chico ({len(junctions)} intersecciones, "
              f"{len(segments)} tramos).", file=sys.stderr)
        sys.exit(1)

    graph_nodes, edges = build_final_graph(nodes, segments)
    restaurants = pick_restaurants(amenities, junctions, nodes)
    bounds = generate_png(root / "data" / "equipetrol.png", bbox)
    emit_json(nodes, graph_nodes, edges, junctions, restaurants, bounds,
              root / "config" / "equipetrol.json")
    print("OK", file=sys.stderr)


if __name__ == "__main__":
    main()
