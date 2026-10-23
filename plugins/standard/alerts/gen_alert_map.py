# /// script
# requires-python = ">=3.11"
# dependencies = ["shapely==2.2.0"]  # map.bin depends on its GEOS; a change moves pixels
# ///
"""Rasterise Ukraine's oblast boundaries into the map the alerts plugin draws.

Inputs, not kept in the repository:
- geoBoundaries UKR ADM1 GeoJSON, full resolution (OpenStreetMap data, ODbL):
  https://github.com/wmgeolab/geoBoundaries/raw/9469f09/releaseData/gbOpen/UKR/ADM1/geoBoundaries-UKR-ADM1.geojson
- Natural Earth 50m land (public domain), to cut away the sea the oblast polygons take in
  along the coast:
  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson

Output: map.bin beside this script, MAP_W x MAP_H bytes, row major. A byte is a region's
index (the order of REGIONS, which plugin.c and plugin.json keep), EDGE on a border
between two regions, COAST on the country's outline, OUTSIDE beyond it. Border pixels
belong to neither region, so a border looks the same whichever side is under alert.

Usage: uv run plugins/standard/alerts/gen_alert_map.py <adm1.geojson> <ne_50m_land.geojson>
           [alerts.json] [--tolerance PX] [--coast-tolerance PX]
The optional alerts sample (a saved answer of neptun.in.ua/api/v1/alerts) is checked
against the names.
"""

import argparse
import json
import math
import os
from collections import Counter, deque
from itertools import pairwise

import numpy as np
import shapely
from shapely.geometry import shape

MAP_W = 160
MAP_H = 128
OUTSIDE = 0xFF
EDGE = 0x40
COAST = 0x41
SS = 8  # each screen pixel is voted on by SS x SS samples
# Fragments of a region smaller than this many pixels join the neighbour they touch most.
MIN_PART = 4

# ISO 3166-2 code -> region name as the alerts API (neptun.in.ua) spells it, in the
# plugin's region order.
REGIONS = [
    ("UA-05", "Вінницька область"),
    ("UA-07", "Волинська область"),
    ("UA-12", "Дніпропетровська область"),
    ("UA-14", "Донецька область"),
    ("UA-18", "Житомирська область"),
    ("UA-21", "Закарпатська область"),
    ("UA-23", "Запорізька область"),
    ("UA-26", "Івано-Франківська область"),
    ("UA-32", "Київська область"),
    ("UA-35", "Кіровоградська область"),
    ("UA-09", "Луганська область"),
    ("UA-46", "Львівська область"),
    ("UA-48", "Миколаївська область"),
    ("UA-51", "Одеська область"),
    ("UA-53", "Полтавська область"),
    ("UA-56", "Рівненська область"),
    ("UA-59", "Сумська область"),
    ("UA-61", "Тернопільська область"),
    ("UA-63", "Харківська область"),
    ("UA-65", "Херсонська область"),
    ("UA-68", "Хмельницька область"),
    ("UA-71", "Черкаська область"),
    ("UA-77", "Чернівецька область"),
    ("UA-74", "Чернігівська область"),
    ("UA-43", "Автономна Республіка Крим"),
    ("UA-30", "м. Київ"),
    ("UA-40", "Севастополь"),
]

# The two cities are a few pixels each: a border around them would leave nothing to fill,
# so they get none and show only when their alert differs from the oblast around them.
CITIES = {"UA-30", "UA-40"}


def screen_transform(regions):
    """The projection onto the panel: longitude scaled by cos(mid latitude), the country
    fitted and centred."""
    lon0, lat0, lon1, lat1 = shapely.total_bounds(regions)
    kx = math.cos(math.radians((lat0 + lat1) / 2))
    scale = min(MAP_W / ((lon1 - lon0) * kx), MAP_H / (lat1 - lat0))
    off_x = (MAP_W - (lon1 - lon0) * kx * scale) / 2
    off_y = (MAP_H - (lat1 - lat0) * scale) / 2

    def to_screen(coords):
        x = off_x + (coords[:, 0] - lon0) * kx * scale
        y = off_y + (lat1 - coords[:, 1]) * scale
        return np.column_stack([x, y])

    return to_screen


def even_odd_fill(buf, rings, value):
    """Fill `value` into the SS-times-finer grid `buf` inside `rings` (screen pixels),
    even-odd rule. Each edge is handed to the rows it crosses, so the cost follows the
    outline length, not the filled area."""
    wide = MAP_W * SS
    rows = MAP_H * SS
    crossings = {}
    for ring in rings:
        pts = [(x * SS, y * SS) for x, y in ring]
        for (x0, y0), (x1, y1) in pairwise(pts):
            if y0 == y1:
                continue
            lo, hi = sorted((y0, y1))
            for row in range(max(0, math.ceil(lo - 0.5)), min(rows, math.ceil(hi - 0.5))):
                cy = row + 0.5
                crossings.setdefault(row, []).append(x0 + (cy - y0) * (x1 - x0) / (y1 - y0))
    for row, xs in crossings.items():
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            first = max(0, math.ceil(a - 0.5))
            last = min(wide - 1, math.floor(b - 0.5))
            if last >= first:
                buf[row * wide + first : row * wide + last + 1] = bytes([value]) * (
                    last - first + 1
                )


def rings_of(geom):
    for poly in getattr(geom, "geoms", [geom]):
        yield list(poly.exterior.coords)
        for hole in poly.interiors:
            yield list(hole.coords)


def majority(fine):
    """Every screen pixel takes the value most of its samples have."""
    wide = MAP_W * SS
    labels = bytearray(MAP_W * MAP_H)
    for py in range(MAP_H):
        block = [fine[(py * SS + k) * wide : (py * SS + k + 1) * wide] for k in range(SS)]
        for px in range(MAP_W):
            votes = Counter()
            for line in block:
                votes.update(line[px * SS : px * SS + SS])
            labels[py * MAP_W + px] = votes.most_common(1)[0][0]
    return labels


def sides(i):
    """The 4 neighbours of pixel i inside the map."""
    x, y = i % MAP_W, i // MAP_W
    if x > 0:
        yield i - 1
    if x < MAP_W - 1:
        yield i + 1
    if y > 0:
        yield i - MAP_W
    if y < MAP_H - 1:
        yield i + MAP_W


def parts(labels, value):
    """The 4-connected pieces of `value`, largest first."""
    seen = set()
    found = []
    for start, v in enumerate(labels):
        if v != value or start in seen:
            continue
        piece = [start]
        seen.add(start)
        queue = deque(piece)
        while queue:
            for n in sides(queue.popleft()):
                if labels[n] == value and n not in seen:
                    seen.add(n)
                    piece.append(n)
                    queue.append(n)
        found.append(piece)
    return sorted(found, key=len, reverse=True)


def smooth_coast(labels):
    """One pass over the coast: a land pixel with the sea on three sides goes to the
    sea, a sea pixel with land on three sides goes to the land it touches most. It takes
    the tip off a one-pixel spike and fills a one-pixel notch; a spit keeps its length.
    Returns how many pixels changed."""
    before = bytes(labels)

    def side_values(i):
        values = [before[n] for n in sides(i)]
        return values + [OUTSIDE] * (4 - len(values))  # beyond the map's edge is sea

    changed = 0
    for i, v in enumerate(before):
        around_i = side_values(i)
        land = [n for n in around_i if n != OUTSIDE]
        if v != OUTSIDE and len(land) <= 1:
            labels[i] = OUTSIDE
            changed += 1
        elif v == OUTSIDE and len(land) >= 3:
            labels[i] = Counter(land).most_common(1)[0][0]
            changed += 1
    return changed


def merge_fragments(labels, values):
    """Hands every piece of a region (or of the sea) but its largest to the neighbour it
    touches most, unless it has MIN_PART pixels or more (a real exclave or island).
    Returns how many pixels moved."""
    moved = 0
    for value in values:
        for piece in parts(labels, value)[1:]:
            if len(piece) >= MIN_PART:
                continue
            inside = set(piece)
            touch = Counter(labels[n] for i in piece for n in sides(i) if n not in inside)
            if touch:
                for i in piece:
                    labels[i] = touch.most_common(1)[0][0]
                moved += len(piece)
    return moved


def with_borders(labels, city_values):
    """Marks the borders. COAST on every land pixel next to the sea or the map's edge.
    EDGE on a land pixel whose right or lower neighbour is another region: one pixel wide,
    and drawn on its own, so neither region owns it. Cities get no border."""

    def at(x, y):
        if 0 <= x < MAP_W and 0 <= y < MAP_H:
            return labels[y * MAP_W + x]
        return OUTSIDE

    out = bytearray(labels)
    for y in range(MAP_H):
        for x in range(MAP_W):
            me = at(x, y)
            if me == OUTSIDE:
                continue
            four = [at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)]
            if OUTSIDE in four:
                out[y * MAP_W + x] = COAST
                continue
            if me in city_values:
                continue
            later = [at(x + 1, y), at(x, y + 1)]
            if any(n != me and n not in city_values for n in later):
                out[y * MAP_W + x] = EDGE
    return out


def around(i):
    """The 8 neighbours of pixel i inside the map."""
    x, y = i % MAP_W, i // MAP_W
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if (dx or dy) and 0 <= x + dx < MAP_W and 0 <= y + dy < MAP_H:
                yield (y + dy) * MAP_W + x + dx


def touching(a, b):
    return abs(a % MAP_W - b % MAP_W) <= 1 and abs(a // MAP_W - b // MAP_W) <= 1


def is_corner(i, line):
    """True when i is the corner of a staircase in `line`: two of its line neighbours
    meet at a right angle at i, and every other line neighbour touches one of the two, so
    the line stays connected without i."""
    straight = [n for n in sides(i) if n in line]
    for a in straight:
        for b in straight:
            if a < b and a % MAP_W != b % MAP_W and a // MAP_W != b // MAP_W:
                rest = [n for n in line if n not in (a, b)]
                if all(touching(n, a) or touching(n, b) for n in rest):
                    return True
    return False


def thin(out, labels):
    """Drops the corner of every staircase, so the borders and the coast are one
    8-connected pixel wide whatever their direction. A border pixel goes back to its
    region only where that region then touches no other one; a coast pixel goes to the
    sea only where no fill or border then touches the sea. Returns how many were dropped."""
    dropped = 0
    changed = True
    while changed:
        changed = False
        for i, v in enumerate(out):
            if v not in (EDGE, COAST):
                continue
            # The coast stays one closed line on its own; a border may end on the coast.
            kinds = (COAST,) if v == COAST else (EDGE, COAST)
            line = [n for n in around(i) if out[n] in kinds]
            if not is_corner(i, line):
                continue
            if v == COAST:
                if any(out[n] not in (COAST, OUTSIDE) for n in sides(i)):
                    continue
                out[i] = OUTSIDE
            else:
                me = labels[i]
                if any(out[n] < EDGE and out[n] != me for n in sides(i)):
                    continue
                out[i] = me
            dropped += 1
            changed = True
    return dropped


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("adm1")
    ap.add_argument("land")
    ap.add_argument("alerts", nargs="?")
    ap.add_argument("--coast-tolerance", type=float, default=1.0, help="every edge's, px")
    ap.add_argument("--tolerance", type=float, default=1.5, help="then the inner borders', px")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "map.bin"))
    args = ap.parse_args()

    with open(args.adm1, encoding="utf-8") as f:
        by_iso = {
            ft["properties"]["shapeISO"]: shape(ft["geometry"]) for ft in json.load(f)["features"]
        }
    missing = [iso for iso, _ in REGIONS if iso not in by_iso]
    if missing:
        raise SystemExit("GeoJSON lacks regions: " + ", ".join(missing))
    regions = [by_iso[iso] for iso, _ in REGIONS]
    to_screen = screen_transform(regions)
    regions = [shapely.transform(g, to_screen) for g in regions]

    # Cut the sea away, then simplify all the regions together: a shared border is
    # simplified once, so the two sides keep meeting exactly.
    with open(args.land, encoding="utf-8") as f:
        land = shapely.union_all([shape(ft["geometry"]) for ft in json.load(f)["features"]])
    bounds = shapely.total_bounds([by_iso[iso] for iso, _ in REGIONS])
    land = shapely.transform(shapely.clip_by_rect(land, *bounds), to_screen)
    regions = [shapely.make_valid(g.intersection(land)) for g in regions]
    regions = [
        shapely.union_all([p for p in shapely.get_parts(g) if p.geom_type == "Polygon"])
        for g in regions
    ]
    if not shapely.coverage_is_valid(regions):
        raise SystemExit("the regions cut to the land overlap or do not share their borders")
    regions = shapely.coverage_simplify(regions, args.coast_tolerance)
    regions = shapely.coverage_simplify(regions, args.tolerance, simplify_boundary=False)

    fine = bytearray([OUTSIDE]) * (MAP_W * SS * MAP_H * SS)
    for index, geom in enumerate(regions):
        even_odd_fill(fine, list(rings_of(geom)), index)
    labels = majority(fine)
    smoothed = smooth_coast(labels)

    moved = merge_fragments(labels, [*range(len(REGIONS)), OUTSIDE])
    city_values = {i for i, (iso, _) in enumerate(REGIONS) if iso in CITIES}
    for index, geom in enumerate(regions):
        if index not in labels:
            # Smaller than a pixel: keep one where it is, so it stays on the map.
            c = geom.representative_point()
            labels[min(MAP_H - 1, int(c.y)) * MAP_W + min(MAP_W - 1, int(c.x))] = index
    out = with_borders(labels, city_values)
    thinned = thin(out, labels)
    with open(args.out, "wb") as f:
        f.write(out)

    print(
        f"{smoothed} coast px smoothed, {moved} px of stray fragments merged, "
        f"{thinned} line corners dropped"
    )
    for index, (iso, name) in enumerate(REGIONS):
        print(f"{iso:<6} {out.count(index):4} px  {name}")

    if args.alerts:
        with open(args.alerts, encoding="utf-8") as f:
            sample = json.load(f)
        api_names = {e["oblast"] for key in ("oblasts", "raions") for e in sample.get(key, [])}
        ours = {name for _, name in REGIONS}
        print("API names not on the map:", sorted(api_names - ours) or "none")
        print("Map regions not in the API:", sorted(ours - api_names) or "none")


if __name__ == "__main__":
    main()
