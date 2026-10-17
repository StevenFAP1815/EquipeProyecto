#!/usr/bin/env python3
"""
Genera config/equipetrol.json y data/equipetrol.png desde OpenStreetMap,
restringido al area definida en data/area.geojson.

Estrategia:
  1. Cargar poligono de area.geojson.
  2. Bbox de consulta = bbox del poligono.
  3. Consultar Overpass con highways principales + residential.
  4. Filtrar intersecciones/segmentos DENTRO del poligono.
  5. Componente conexa mayor.
  6. Submuestreo si hay demasiados.
  7. PNG con bbox calculado del poligono.

Datos: (c) OpenStreetMap contributors, licencia ODbL.
"""

import json
import math
import sys
from collections import Counter, defaultdict, deque
from pathlib import Path

import requests
from staticmap import StaticMap

# ---------------------------------------------------------------------------
# Configuracion.
# ---------------------------------------------------------------------------

IMAGE_WIDTH  = 2560
IMAGE_HEIGHT = 1706

OVERPASS_URL = "https://overpass-api.de/api/interpreter"

HIGHWAY_FILTER_STRICT = ["primary", "secondary", "tertiary", "trunk", "unclassified"]
HIGHWAY_FILTER_RELAXED = HIGHWAY_FILTER_STRICT + ["residential"]

AMENITY_FILTER = ["restaurant", "fast_food", "cafe"]

MIN_INTERSECTIONS = 25
MIN_SEGMENTS = 40
MAX_INTERSECTIONS = 120
MIN_RESTAURANTS = 6

EARTH_RADIUS_M = 6371000.0


# ---------------------------------------------------------------------------
# Geometria.
# ---------------------------------------------------------------------------

def haversine(lat1, lon1, lat2, lon2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    a = (math.sin(dphi / 2) ** 2
         + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2)
    return EARTH_RADIUS_M * 2 * math.asin(min(1.0, math.sqrt(a)))


def point_in_polygon(lat, lon, polygon):
    """Ray-casting. polygon: lista de (lon, lat)."""
    n = len(polygon)
    inside = False
    j = n - 1
    for i in range(n):
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
    elif gj.get("type") == "Polygon":
        coords = gj["coordinates"][0]
    else:
        raise ValueError("No es Polygon ni Feature con Polygon")
    return [(lon, lat) for lon, lat in coords]


def polygon_bbox(polygon):
    lons = [p[0] for p in polygon]
    lats = [p[1] for p in polygon]
    return {
        "north": max(lats), "south": min(lats),
        "west":  min(lons), "east":  max(lons),
    }


def mercator_y(lat_deg):
    lat = math.radians(lat_deg)
    return (1 - math.log(math.tan(lat) + 1 / math.cos(lat)) / math.pi) / 2


def calculate_zoom_for_bbox(bbox, width, height):
    x_w = (bbox["west"] + 180) / 360
    x_e = (bbox["east"] + 180) / 360
    y_n = mercator_y(bbox["north"])
    y_s = mercator_y(bbox["south"])
    dx = x_e - x_w
    dy = y_s - y_n
    if dx <= 0 or dy <= 0:
        return 14
    zoom_x = math.log2(width / (dx * 256))
    zoom_y = math.log2(height / (dy * 256))
    return max(1, min(19, int(min(zoom_x, zoom_y))))


def pixel_to_latlon(px, py, zoom, center_lat, center_lon, width, height):
    scale = 256.0 * (2 ** zoom)
    world_x = (center_lon + 180.0) / 360.0 * scale
    world_y = mercator_y(center_lat) * scale
    px_x = world_x + (px - width / 2.0)
    px_y = world_y + (py - height / 2.0)
    lon = px_x / scale * 360.0 - 180.0
    n = math.pi - 2.0 * math.pi * px_y / scale
    lat = math.degrees(math.atan(math.sinh(n)))
    return lat, lon


def compute_real_bbox(zoom, center_lat, center_lon, width, height):
    nw_lat, nw_lon = pixel_to_latlon(0, 0, zoom, center_lat, center_lon, width, height)
    se_lat, se_lon = pixel_to_latlon(width, height, zoom, center_lat, center_lon, width, height)
    return {"north": nw_lat, "south": se_lat, "west": nw_lon, "east": se_lon}


# ---------------------------------------------------------------------------
# Overpass.
# ---------------------------------------------------------------------------

def fetch_osm_data(bbox, highway_filter):
    highway_regex = "|".join(highway_filter)
    amenity_regex = "|".join(AMENITY_FILTER)
    query = f"""
    [out:json][timeout:120];
    (
      way["highway"~"^({highway_regex})$"]
         ({bbox["south"]},{bbox["west"]},{bbox["north"]},{bbox["east"]});
      node["amenity"~"^({amenity_regex})$"]
         ({bbox["south"]},{bbox["west"]},{bbox["north"]},{bbox["east"]});
    );
    out body;
    >;
    out skel qt;
    """
    print(f"[1/7] Consultando Overpass (bbox: {bbox})...", file=sys.stderr)
    headers = {'User-Agent': 'DeliverySim-ConfigScript/1.0'}
    r = requests.post(OVERPASS_URL, data={"data": query}, headers=headers, timeout=180)
    r.raise_for_status()
    return r.json()


# ---------------------------------------------------------------------------
# Parseo.
# ---------------------------------------------------------------------------

def parse_osm(osm):
    nodes = {}
    ways = []
    amenities = []
    for el in osm.get("elements", []):
        if el.get("type") == "node":
            if "lat" in el and "lon" in el:
                nodes[el["id"]] = (el["lat"], el["lon"])
                if "amenity" in el.get("tags", {}):
                    amenities.append({
                        "id": el["id"], "lat": el["lat"], "lon": el["lon"],
                        "amenity": el["tags"]["amenity"],
                        "name": el["tags"].get("name", "Unnamed"),
                    })
        elif el.get("type") == "way":
            tags = el.get("tags", {})
            if "highway" in tags:
                ways.append({
                    "id": el["id"], "highway": tags["highway"],
                    "nodes": el.get("nodes", []),
                    "oneway": tags.get("oneway", "no"),
                })
    print(f"[2/7] {len(nodes)} nodos, {len(ways)} vias, {len(amenities)} POIs.",
          file=sys.stderr)
    return nodes, ways, amenities


def build_graph(nodes, ways):
    hw_counts = Counter(w["highway"] for w in ways)
    print(f"  Distribucion highways: {dict(hw_counts)}", file=sys.stderr)

    node_way_count = defaultdict(int)
    for w in ways:
        for nid in w["nodes"]:
            node_way_count[nid] += 1

    intersections = {nid: nodes[nid]
                     for nid, deg in node_way_count.items()
                     if deg >= 3 and nid in nodes}

    print(f"[3/7] {len(intersections)} intersecciones totales.",
          file=sys.stderr)

    segments = []
    for w in ways:
        path = w["nodes"]
        i = 0
        while i < len(path):
            start = path[i]
            if start not in intersections:
                i += 1
                continue
            j = i + 1
            while j < len(path) and path[j] not in intersections:
                j += 1
            if j < len(path):
                segments.append({
                    "from": start, "to": path[j],
                    "oneway": w["oneway"].lower() in ("yes", "true", "1", "-1"),
                    "reverse": w["oneway"] == "-1",
                })
                i = j
            else:
                break

    seen = set()
    unique = []
    for s in segments:
        a, b = s["from"], s["to"]
        if s["reverse"]:
            a, b = b, a
        key = (min(a, b), max(a, b))
        if key in seen:
            continue
        seen.add(key)
        unique.append({"from": a, "to": b, "oneway": s["oneway"]})

    print(f"[4/7] {len(unique)} segmentos unicos.", file=sys.stderr)
    return intersections, unique


def filter_by_polygon(intersections, segments, polygon):
    """
    Incluye:
    - Intersecciones dentro del poligono.
    - Intersecciones FUERA del poligono que tengan al menos una calle
      que conecte con una interseccion dentro (nodos de borde).
    - Segmentos con al menos un extremo dentro del poligono.
    """
    inside = {nid for nid, (lat, lon) in intersections.items()
              if point_in_polygon(lat, lon, polygon)}

    # Nodos de borde: fuera, pero conectados a uno dentro.
    border = set()
    for s in segments:
        if s["from"] in inside and s["to"] not in inside:
            border.add(s["to"])
        elif s["to"] in inside and s["from"] not in inside:
            border.add(s["from"])

    keep = inside | border

    ni = {nid: intersections[nid] for nid in keep}
    ns = [s for s in segments
          if s["from"] in keep and s["to"] in keep
          and (s["from"] in inside or s["to"] in inside)]
    print(f"[5/7] Dentro+borde: {len(ni)} nodos, {len(ns)} segmentos.",
          file=sys.stderr)
    return ni, ns

def largest_component(intersections, segments):
    adj = defaultdict(set)
    for s in segments:
        adj[s["from"]].add(s["to"])
        adj[s["to"]].add(s["from"])
    if not intersections:
        return {}, []

    start = max(intersections.keys(), key=lambda n: len(adj.get(n, ())))
    visited = {start}
    q = deque([start])
    while q:
        u = q.popleft()
        for v in adj.get(u, ()):
            if v not in visited:
                visited.add(v)
                q.append(v)

    ni = {nid: c for nid, c in intersections.items() if nid in visited}
    ns = [s for s in segments if s["from"] in visited and s["to"] in visited]
    print(f"[6/7] Componente mayor: {len(ni)} nodos, {len(ns)} segmentos.",
          file=sys.stderr)
    return ni, ns


# ---------------------------------------------------------------------------
# Restaurantes y PNG.
# ---------------------------------------------------------------------------

def pick_restaurants(amenities, intersections):
    valid = [a for a in amenities if a["amenity"] in AMENITY_FILTER]
    if not valid:
        print("ERROR: sin restaurantes.", file=sys.stderr)
        sys.exit(1)

    def nearest(lat, lon):
        return min(intersections.keys(),
                   key=lambda nid: haversine(lat, lon, *intersections[nid]))

    for a in valid:
        a["node"] = nearest(a["lat"], a["lon"])

    center_lat = sum(v[0] for v in intersections.values()) / len(intersections)
    center_lon = sum(v[1] for v in intersections.values()) / len(intersections)
    valid.sort(key=lambda a: haversine(a["lat"], a["lon"],
                                       center_lat, center_lon))

    chosen = [valid[0]]
    remaining = valid[1:]
    while len(chosen) < MIN_RESTAURANTS and remaining:
        b = max(remaining, key=lambda a: min(
            haversine(a["lat"], a["lon"], c["lat"], c["lon"]) for c in chosen))
        chosen.append(b)
        remaining.remove(b)

    if len(chosen) < MIN_RESTAURANTS:
        print(f"ERROR: solo {len(chosen)} restaurantes.", file=sys.stderr)
        sys.exit(1)
    return chosen


def generate_png(output_path, bbox, polygon, real_bbox_ref):
    # Calcular zoom para cubrir el bbox COMPLETO del polígono.
    # No forzamos +1. El zoom calculado ya es el que cubre el bbox.
    zoom = calculate_zoom_for_bbox(bbox, IMAGE_WIDTH, IMAGE_HEIGHT)

    # Centroide del polígono (más preciso que el centro del bbox).
    n = len(polygon)
    clat = sum(p[1] for p in polygon) / n
    clon = sum(p[0] for p in polygon) / n

    print(f"  PNG {IMAGE_WIDTH}x{IMAGE_HEIGHT}, zoom={zoom} "
          f"(center: {clat:.5f}, {clon:.5f})...", file=sys.stderr)

    m = StaticMap(IMAGE_WIDTH, IMAGE_HEIGHT,
                  url_template="https://tile.openstreetmap.org/{z}/{x}/{y}.png")
    image = m.render(zoom=zoom, center=[clon, clat])
    image.save(str(output_path))

    real_bbox_ref.update(compute_real_bbox(zoom, clat, clon,
                                           IMAGE_WIDTH, IMAGE_HEIGHT))
    print(f"  PNG -> {output_path}", file=sys.stderr)


def emit_json(intersections, segments, restaurants, real_bbox, output_path):
    osm_to_nid = {}
    nodes_out = []
    for i, (osm_id, (lat, lon)) in enumerate(sorted(intersections.items())):
        nid = f"n{i}"
        osm_to_nid[osm_id] = nid
        nodes_out.append({"id": nid, "lat": lat, "lon": lon})

    degree = defaultdict(int)
    for s in segments:
        degree[s["from"]] += 1
        degree[s["to"]] += 1
    start_node = osm_to_nid[max(degree, key=lambda n: degree[n])]

    streets_out = [{
        "id": f"s{i}", "from": osm_to_nid[s["from"]],
        "to": osm_to_nid[s["to"]], "oneWay": s["oneway"],
    } for i, s in enumerate(segments)]

    restaurants_out = [{
        "id": f"r{i}", "name": r["name"], "node": osm_to_nid[r["node"]],
        "pickupSlots": 1, "prepTimeMs": [60000, 240000],
    } for i, r in enumerate(restaurants)]

    config = {
        "map": {
            "image": "data/equipetrol.png",
            "attribution": "(c) OpenStreetMap contributors",
            "bounds": real_bbox,
        },
        "nodes": nodes_out,
        "streets": streets_out,
        "restaurants": restaurants_out,
        "fleet": {"couriers": 8, "bagCapacity": 3,
                  "speedKmh": 30, "startNode": start_node},
        "orders": {"meanIntervalMs": 20000, "burstMax": 12,
                   "maxPending": 50, "seed": 42},
        "dispatch": {"quoteTimeoutMs": 200, "acceptTimeoutMs": 600000},
        "incidents": {"breakdownProbability": 0.02},
        "simulation": {"durationS": 3600, "timeScale": 60},
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)
    print(f"[7/7] JSON -> {output_path}", file=sys.stderr)
    print(f"  Resumen: {len(nodes_out)} nodos, {len(streets_out)} calles, "
          f"{len(restaurants_out)} restaurantes.", file=sys.stderr)


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------

def attempt(bbox, highway_filter, polygon):
    osm = fetch_osm_data(bbox, highway_filter)
    nodes, ways, amenities = parse_osm(osm)
    intersections, segments = build_graph(nodes, ways)
    intersections, segments = filter_by_polygon(intersections, segments, polygon)
    intersections, segments = largest_component(intersections, segments)
    return intersections, segments, amenities


def main():
    repo_root = Path(__file__).resolve().parent.parent
    config_path = repo_root / "config" / "equipetrol.json"
    png_path = repo_root / "data" / "equipetrol.png"
    polygon_path = repo_root / "data" / "area.geojson"

    if not polygon_path.exists():
        print(f"ERROR: {polygon_path} no existe.", file=sys.stderr)
        sys.exit(1)

    polygon = load_polygon(polygon_path)
    bbox = polygon_bbox(polygon)
    print(f"Bbox del poligono: {bbox}", file=sys.stderr)

    intersections, segments, amenities = attempt(bbox, HIGHWAY_FILTER_STRICT, polygon)

    if len(intersections) < MIN_INTERSECTIONS or len(segments) < MIN_SEGMENTS:
        print(f"  Estricto insuficiente ({len(intersections)} nodos, "
              f"{len(segments)} segmentos). Reintentando con residential.",
              file=sys.stderr)
        intersections, segments, amenities = attempt(bbox, HIGHWAY_FILTER_RELAXED, polygon)

    if len(intersections) < MIN_INTERSECTIONS:
        print(f"ERROR: {len(intersections)} < {MIN_INTERSECTIONS}.", file=sys.stderr)
        sys.exit(1)
    if len(segments) < MIN_SEGMENTS:
        print(f"ERROR: {len(segments)} < {MIN_SEGMENTS}.", file=sys.stderr)
        sys.exit(1)

    restaurants = pick_restaurants(amenities, intersections)
    real_bbox = {}
    generate_png(png_path, bbox, polygon, real_bbox)
    emit_json(intersections, segments, restaurants, real_bbox, config_path)
    print("OK", file=sys.stderr)


if __name__ == "__main__":
    main()