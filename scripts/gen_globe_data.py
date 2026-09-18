#!/usr/bin/env python3
"""
gen_globe_data.py -- build the fixed geometry the globe screen draws.

    ./scripts/gen_globe_data.py                 regenerate the header + preview
    ./scripts/gen_globe_data.py --tolerance-px 1.4 --view 33,-117

Writes firmware/src/ui/globe_data.h (coastlines, graticule, equator) and
globe_preview.svg, which renders that exact table through the exact
projection the firmware uses -- so it is a preview, not a second drawing that
might flatter the first.

Coastlines are Natural Earth 1:110m land polygons (public domain).

Why unit vectors and not lat/lon
--------------------------------
The obvious table is a list of (lat, lon) pairs, and it is the wrong one.
Projecting a lat/lon point needs the sine and cosine of its latitude and of
its longitude offset, and on an ESP32 `double` trig is *software* -- the FPU
is single-precision only. Across the coastline and the graticule that is
roughly 12,000 software trig calls per frame, which does not fit in a 100 ms
budget that also has three SPI panel pushes in it.

So the trig is done HERE, once, at build time. Every vertex ships as its
position on the unit sphere in Earth-centred, Earth-fixed axes, quantised to
three int16s. Projecting one is then nine float multiplies against a rotation
the firmware builds once per frame -- no trig in the inner loop at all. The
cost is 6 bytes of flash per vertex, which the ESP32 has in abundance and
which never touches RAM.

Quantising to int16 is a 1/32767 rounding error on a unit sphere: about
0.003 px on a 104 px globe. Invisible.
"""

import argparse
import json
import math
import pathlib
import sys
import urllib.request

# Natural Earth 1:110m land polygons. Public domain, no attribution required.
SOURCE_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
    "master/geojson/ne_110m_land.geojson"
)

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "firmware" / "src" / "ui" / "globe_data.h"
MASK_HEADER = ROOT / "firmware" / "src" / "ui" / "land_mask.h"
PREVIEW = ROOT / "firmware" / "src" / "ui" / "globe_preview.png"
CACHE = ROOT / ".cache" / "ne_110m_land.geojson"

# Must match cfg::SCREEN_* and GLOBE_R in globe_screen.cpp. Used only to turn
# the pixel-denominated knobs below into angles; nothing is emitted in pixels.
GLOBE_R_PX = 104
SCREEN_W = 240

SCALE = 32767.0

# Graticule spacing, degrees. STEP is how finely each line is walked -- it is
# a straight line in 3D between samples, so too coarse shows as a polygon.
GRAT_SPACING = 30
GRAT_STEP = 5
EQUATOR_STEP = 4

# Working raster for the land mask, in degrees per cell. Only ever an
# intermediate -- the shipped table is the cube map built from it.
EQUIRECT_W, EQUIRECT_H = 2880, 1440


# ---------------------------------------------------------------------------
# Geometry. Everything is on the unit sphere, so a "distance" is a 3D chord:
# 1.0 unit == one Earth radius == GLOBE_R_PX pixels at the centre of the disc.
# ---------------------------------------------------------------------------

def to_unit(lon_deg, lat_deg):
    """GeoJSON order is (lon, lat). ECEF axes: +z north pole, +x at 0N 0E."""
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    cos_lat = math.cos(lat)
    return (cos_lat * math.cos(lon), cos_lat * math.sin(lon), math.sin(lat))


def seg_distance(p, a, b):
    """Perpendicular distance from p to segment ab, in 3D."""
    abx, aby, abz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    apx, apy, apz = p[0] - a[0], p[1] - a[1], p[2] - a[2]
    ab2 = abx * abx + aby * aby + abz * abz
    if ab2 <= 0.0:
        return math.sqrt(apx * apx + apy * apy + apz * apz)
    t = (apx * abx + apy * aby + apz * abz) / ab2
    t = max(0.0, min(1.0, t))
    dx = apx - t * abx
    dy = apy - t * aby
    dz = apz - t * abz
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def douglas_peucker(points, eps):
    """Iterative DP -- some Natural Earth rings are deep enough to blow
    Python's recursion limit, and an explicit stack costs nothing.

    Simplifying in 3D rather than in lat/lon is what keeps Greenland and the
    Antarctic coast from being chewed up: a degree of longitude is 111 km at
    the equator and a few hundred metres at 80 N, so a lat/lon tolerance means
    something different in every frame of the picture.
    """
    n = len(points)
    if n < 3:
        return list(points)
    keep = [False] * n
    keep[0] = keep[n - 1] = True
    stack = [(0, n - 1)]
    while stack:
        first, last = stack.pop()
        if last <= first + 1:
            continue
        worst, worst_i = -1.0, -1
        a, b = points[first], points[last]
        for i in range(first + 1, last):
            d = seg_distance(points[i], a, b)
            if d > worst:
                worst, worst_i = d, i
        if worst > eps:
            keep[worst_i] = True
            stack.append((first, worst_i))
            stack.append((worst_i, last))
    return [p for p, k in zip(points, keep) if k]


def is_synthetic(a, b):
    """True for edges Natural Earth invented, not coastline anyone can see.

    The polygons are clipped to the plate-carree rectangle, so Antarctica is
    closed off with a seam running down the antimeridian, along lat -90, and
    back up. On a globe those become three straight lines converging on the
    south pole -- unmistakably wrong, and the first thing you would notice.
    Both endpoints sitting on the same artificial edge is the tell.
    """
    (lon_a, lat_a), (lon_b, lat_b) = a, b
    if lat_a <= -89.0 and lat_b <= -89.0:
        return True
    if lat_a >= 89.0 and lat_b >= 89.0:
        return True
    if abs(lon_a) >= 179.5 and abs(lon_b) >= 179.5:
        return True
    return False


def ring_extent(ring_xyz):
    """Largest span of the ring along any axis -- a cheap stand-in for "how
    big does this island land on screen". Chord units, so multiplying by
    GLOBE_R_PX gives pixels at the centre of the disc."""
    xs = [p[0] for p in ring_xyz]
    ys = [p[1] for p in ring_xyz]
    zs = [p[2] for p in ring_xyz]
    return max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


# ---------------------------------------------------------------------------

def load_geojson(refresh=False):
    if CACHE.exists() and not refresh:
        print(f"using cached {CACHE.relative_to(ROOT)}")
        return json.loads(CACHE.read_text())
    print(f"downloading {SOURCE_URL}")
    with urllib.request.urlopen(SOURCE_URL, timeout=60) as resp:
        raw = resp.read().decode("utf-8")
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(raw)
    return json.loads(raw)


def rings_from(geojson):
    """Every linear ring in the file, as lists of (lon, lat). Holes included --
    the shore of an inland sea is a coastline too."""
    for feature in geojson["features"]:
        geom = feature["geometry"]
        kind = geom["type"]
        if kind == "Polygon":
            polys = [geom["coordinates"]]
        elif kind == "MultiPolygon":
            polys = geom["coordinates"]
        else:
            continue
        for poly in polys:
            for ring in poly:
                yield [(float(c[0]), float(c[1])) for c in ring]


def build_coastlines(geojson, tolerance, min_feature):
    """Rings -> simplified open strokes of unit vectors, seams removed."""
    strokes = []
    raw_vertices = 0
    dropped_small = 0

    for ring in rings_from(geojson):
        raw_vertices += len(ring)
        xyz = [to_unit(lon, lat) for lon, lat in ring]
        if ring_extent(xyz) < min_feature:
            dropped_small += 1
            continue

        # Cut the ring wherever the rectangle seam runs, which turns one closed
        # ring into one or more open strokes.
        run = [xyz[0]]
        pieces = []
        for i in range(1, len(ring)):
            if is_synthetic(ring[i - 1], ring[i]):
                pieces.append(run)
                run = [xyz[i]]
            else:
                run.append(xyz[i])
        pieces.append(run)

        # A closed ring that got cut starts and ends mid-stroke, so the last
        # piece and the first are really one run through the ring's start
        # point. Rejoin them, or the outline carries a gap nothing put there.
        if len(pieces) > 1 and ring[0] == ring[-1]:
            pieces[0] = pieces[-1] + pieces[0][1:]
            pieces.pop()

        for piece in pieces:
            simplified = douglas_peucker(piece, tolerance)
            if len(simplified) >= 2:
                strokes.append(simplified)

    return strokes, raw_vertices, dropped_small


def build_graticule():
    """Meridians and parallels, every GRAT_SPACING degrees. The equator comes
    back separately because the firmware draws it in a brighter colour."""
    strokes = []
    for lon in range(-180, 180, GRAT_SPACING):
        strokes.append([to_unit(lon, lat)
                        for lat in range(-90, 90 + 1, GRAT_STEP)])
    for lat in range(-90 + GRAT_SPACING, 90, GRAT_SPACING):
        if lat == 0:
            continue                      # the equator is its own table
        strokes.append([to_unit(lon, lat)
                        for lon in range(-180, 180 + 1, GRAT_STEP)])
    equator = [[to_unit(lon, 0) for lon in range(-180, 180 + 1, EQUATOR_STEP)]]
    return strokes, equator


def quantise(strokes):
    out = []
    for stroke in strokes:
        q = []
        for x, y, z in stroke:
            q.append((
                max(-32767, min(32767, int(round(x * SCALE)))),
                max(-32767, min(32767, int(round(y * SCALE)))),
                max(-32767, min(32767, int(round(z * SCALE)))),
            ))
        # Quantising can collapse neighbours onto the same lattice point.
        dedup = [q[0]]
        for p in q[1:]:
            if p != dedup[-1]:
                dedup.append(p)
        if len(dedup) >= 2:
            out.append(dedup)
    return out


# ---------------------------------------------------------------------------
# Land mask. Stroking answers "where is the coast"; filling answers "is THIS
# point land", which is a different question and wants a different structure.
# ---------------------------------------------------------------------------

def rasterise_equirect(geojson):
    """Even-odd scanline fill of the land polygons into a lat/lon bitmap.

    This deliberately uses the ORIGINAL rings, seams and all. The antimeridian
    and lat -90 edges that had to be *removed* for stroking are exactly what
    closes Antarctica for filling -- without them the even-odd rule has an odd
    number of crossings on every Antarctic row and fills the wrong half.
    Fill and stroke want opposite things from the same data.
    """
    buckets = [[] for _ in range(EQUIRECT_H)]
    for ring in rings_from(geojson):
        for (x0, y0), (x1, y1) in zip(ring, ring[1:]):
            if y0 == y1:
                continue                      # horizontal edges cross nothing
            lo, hi = (y0, y1) if y0 < y1 else (y1, y0)
            j0 = max(0, int(math.floor((90.0 - hi) * EQUIRECT_H / 180.0)))
            j1 = min(EQUIRECT_H - 1, int(math.ceil((90.0 - lo) * EQUIRECT_H / 180.0)))
            for j in range(j0, j1 + 1):
                lat = 90.0 - (j + 0.5) * 180.0 / EQUIRECT_H
                # Half-open test, so a vertex landing exactly on the scanline
                # counts once rather than zero or twice.
                if (y0 <= lat) != (y1 <= lat):
                    buckets[j].append(x0 + (lat - y0) * (x1 - x0) / (y1 - y0))

    rows = []
    for j in range(EQUIRECT_H):
        xs = sorted(buckets[j])
        row = bytearray(EQUIRECT_W)
        for k in range(0, len(xs) - 1, 2):
            i0 = max(0, int(round((xs[k] + 180.0) * EQUIRECT_W / 360.0)))
            i1 = min(EQUIRECT_W, int(round((xs[k + 1] + 180.0) * EQUIRECT_W / 360.0)))
            for i in range(i0, i1):
                row[i] = 1
        rows.append(row)
    return rows


def build_cube_map(rows, n):
    """Resample the lat/lon bitmap into a cube map of bits.

    A cube map because the firmware looks this up by DIRECTION, once per pixel
    of the disc. Indexing by lat/lon would need asin and atan2 per pixel; a
    cube face needs the dominant axis and one division. The trig all happens
    here instead.

    Face f and (i, j) map to a direction as laid out below; the firmware's
    landAt() must invert this exactly, so the two are written to be read side
    by side.
    """
    bits = bytearray(6 * n * n // 8)
    land = 0
    for face in range(6):
        for j in range(n):
            tc = (j + 0.5) / n * 2.0 - 1.0
            for i in range(n):
                sc = (i + 0.5) / n * 2.0 - 1.0
                x, y, z = [(1.0, sc, tc), (-1.0, sc, tc),
                           (sc, 1.0, tc), (sc, -1.0, tc),
                           (sc, tc, 1.0), (sc, tc, -1.0)][face]
                r = math.sqrt(x * x + y * y + z * z)
                lat = math.degrees(math.asin(z / r))
                lon = math.degrees(math.atan2(y, x))
                jj = min(EQUIRECT_H - 1, max(0, int((90.0 - lat) * EQUIRECT_H / 180.0)))
                ii = min(EQUIRECT_W - 1, max(0, int((lon + 180.0) * EQUIRECT_W / 360.0)))
                if rows[jj][ii]:
                    idx = face * n * n + j * n + i
                    bits[idx >> 3] |= 1 << (idx & 7)
                    land += 1
    return bits, land / (6 * n * n)


def write_mask_header(bits, n, land_fraction):
    lines = []
    w = lines.append
    w("#pragma once")
    w("//")
    w("// land_mask.h -- GENERATED by scripts/gen_globe_data.py. Do not edit it;")
    w("// re-run the script instead.")
    w("//")
    w("// Is this direction land or ocean? One bit per cube-map texel.")
    w("//")
    w("// The globe fills land by asking that question once per pixel of the disc,")
    w("// ~34,000 times per rebuild. Outlines cannot answer it -- a closed curve")
    w("// does not tell you which side is inside, which is why the coastline alone")
    w("// reads as a tangle of lines rather than as continents.")
    w("//")
    w("// A CUBE MAP, not a lat/lon grid, because looking up a lat/lon costs an")
    w("// asin and an atan2 and this is the hot loop. Finding the dominant axis of")
    w("// a direction vector costs three compares and one divide. See landAt() in")
    w("// globe_screen.cpp, which inverts the mapping the generator used.")
    w("//")
    w(f"// {n}x{n} per face. A texel spans {90.0 / n:.2f} deg, and the globe is")
    w(f"// ~1.16 px/deg at the centre of the disc, so a texel is about")
    w(f"// {90.0 / n * 1.156:.2f} px there -- matched to the screen, not finer.")
    w(f"// Land covers {land_fraction:.3f} of the sphere (Earth is ~0.29).")
    w("//")
    w("// constexpr, so it lives in flash and costs no RAM.")
    w("//")
    w("#include <cstdint>")
    w("")
    w("#include <cmath>")
    w("")
    w("namespace land_mask {")
    w("")
    w(f"constexpr int kFaceN = {n};")
    w(f"constexpr int kByteCount = {len(bits)};")
    w("")
    w(f"inline constexpr uint8_t kBits[kByteCount] = {{")
    for k in range(0, len(bits), 16):
        w("    " + "".join(f"0x{b:02x}," for b in bits[k:k + 16]))
    w("};")
    w("")
    w("// The lookup is emitted with the table on purpose. It is the exact")
    w("// inverse of the mapping build_cube_map() used to fill it in, and the")
    w("// two going out of step would not crash or warn -- it would quietly put")
    w("// Africa in the Pacific. Generating both together means they cannot.")
    w("//")
    w("// `d` must be a UNIT vector in the same Earth-centred axes as")
    w("// globe_data.h. Returns true if that direction is over land.")
    w("inline bool at(float dx, float dy, float dz) {")
    w("    const float ax = std::fabs(dx), ay = std::fabs(dy), az = std::fabs(dz);")
    w("")
    w("    // Which cube face the direction points at: the dominant axis.")
    w("    int face;")
    w("    float m, sc, tc;")
    w("    if (ax >= ay && ax >= az) { m = ax; face = dx > 0.0f ? 0 : 1; sc = dy; tc = dz; }")
    w("    else if (ay >= az)        { m = ay; face = dy > 0.0f ? 2 : 3; sc = dx; tc = dz; }")
    w("    else                      { m = az; face = dz > 0.0f ? 4 : 5; sc = dx; tc = dy; }")
    w("")
    w("    // One reciprocal and two multiplies, NOT two divisions: a float")
    w("    // divide on an ESP32 is a call to __divsf3, and this runs once per")
    w("    // pixel of the disc. `m` cannot be zero -- the largest component of a")
    w("    // unit vector is at least 0.577.")
    w("    const float inv = 1.0f / m;")
    w("    int i = static_cast<int>((sc * inv + 1.0f) * 0.5f * kFaceN);")
    w("    int j = static_cast<int>((tc * inv + 1.0f) * 0.5f * kFaceN);")
    w("    if (i < 0) i = 0; else if (i >= kFaceN) i = kFaceN - 1;")
    w("    if (j < 0) j = 0; else if (j >= kFaceN) j = kFaceN - 1;")
    w("")
    w("    const int idx = (face * kFaceN + j) * kFaceN + i;")
    w("    return (kBits[idx >> 3] >> (idx & 7)) & 1u;")
    w("}")
    w("")
    w("}  // namespace land_mask")
    w("")
    MASK_HEADER.write_text("\n".join(lines))


# ---------------------------------------------------------------------------

def emit_table(w, name, strokes):
    """One flat array per table, strokes separated by the pen-up sentinel."""
    count = sum(len(s) for s in strokes) + len(strokes)
    w(f"constexpr int k{name}Count = {count};")
    w(f"inline constexpr Vertex k{name}[k{name}Count] = {{")
    for stroke in strokes:
        row = []
        for (x, y, z) in stroke:
            row.append("{%d,%d,%d}," % (x, y, z))
            if len(row) == 4:
                w("    " + " ".join(row))
                row = []
        if row:
            w("    " + " ".join(row))
        w("    kPenUp,")
    w("};")
    w("")
    return count


def write_header(coast, grat, equator, stats):
    lines = []
    w = lines.append
    w("#pragma once")
    w("//")
    w("// globe_data.h -- GENERATED by scripts/gen_globe_data.py. Do not edit it;")
    w("// re-run the script instead.")
    w("//")
    w("// The fixed geometry the globe screen draws: coastlines, graticule, equator.")
    w("// Coastlines are Natural Earth 1:110m land (public domain):")
    w(f"//   {stats['raw']} source vertices -> {stats['kept']} kept in "
      f"{stats['strokes']} strokes,")
    w(f"//   simplified at {stats['tolerance_px']:.2f} px, features under "
      f"{stats['min_feature_px']:.1f} px dropped.")
    w("//")
    w("// Every vertex is a point on the UNIT SPHERE in Earth-centred, Earth-fixed")
    w("// axes (+z out the north pole, +x through 0N 0E), scaled by 32767.")
    w("//")
    w("// It is deliberately not lat/lon. Projecting a lat/lon vertex needs the")
    w("// sine and cosine of its latitude and longitude, and `double` trig on an")
    w("// ESP32 is software -- the FPU is single-precision only. Across these")
    w("// tables that would be ~12,000 software trig calls per frame. As unit")
    w("// vectors the same work is nine float multiplies each, against a rotation")
    w("// built once per frame by orbit::makeOrthoView(). No trig in the loop.")
    w("//")
    w("// A {0, 0, 0} vertex is impossible on a unit sphere, so it serves as the")
    w("// pen-up marker between strokes. Drawing a line ACROSS one joins Norway to")
    w("// Chile, which is exactly the bug this encoding exists to make obvious.")
    w("//")
    w("// These are `constexpr`, so they live in flash (.rodata) and are read in")
    w("// place. They cost zero RAM, which is the only budget that is tight here.")
    w("//")
    w("#include <cstdint>")
    w("")
    w("namespace globe_data {")
    w("")
    w("struct Vertex {")
    w("    int16_t x, y, z;")
    w("};")
    w("")
    w("// Multiply a vertex by this to get back to a unit vector. It is the")
    w("// reciprocal and not the scale because the Xtensa FPU has no divide")
    w("// instruction -- a float division is a called routine, and the draw loop")
    w("// would do three of them per vertex. The compiler will not make this")
    w("// substitution for you: 1/32767 is not exact in binary, so folding a")
    w("// divide into a multiply changes the last bit and is only legal under")
    w("// -ffast-math, which this project does not build with.")
    w("constexpr float kInvScale = 1.0f / 32767.0f;")
    w("")
    w("// Pen up: lift, move, put down. Not a point on the sphere.")
    w("constexpr Vertex kPenUp{0, 0, 0};")
    w("")
    w("constexpr bool isPenUp(const Vertex& v) {")
    w("    return v.x == 0 && v.y == 0 && v.z == 0;")
    w("}")
    w("")
    n_coast = emit_table(w, "Coastline", coast)
    n_grat = emit_table(w, "Graticule", grat)
    n_eq = emit_table(w, "Equator", equator)
    w("}  // namespace globe_data")
    w("")
    HEADER.write_text("\n".join(lines))
    return n_coast + n_grat + n_eq


# ---------------------------------------------------------------------------

def write_preview(coast, grat, equator, bits, n, view_lat, view_lon, scale):
    """Render the shipped tables the way the panel will, into a PNG.

    A PNG and not an SVG because the fill is genuinely per-pixel now, and the
    point of a preview is to show what the panel shows. These are real 240x240
    panel pixels, just drawn `scale` times larger so you can see them.

    This mirrors globe_screen.cpp on purpose -- same rotation, same cube-map
    lookup, same stroke loop. If the two ever disagree, this one is wrong.
    """
    import struct
    import zlib

    lat0 = math.radians(view_lat)
    lon0 = math.radians(view_lon)
    sin_lat0, cos_lat0 = math.sin(lat0), math.cos(lat0)
    sin_lon0, cos_lon0 = math.sin(lon0), math.cos(lon0)

    east = (-sin_lon0, cos_lon0, 0.0)
    north = (-sin_lat0 * cos_lon0, -sin_lat0 * sin_lon0, cos_lat0)
    up = (cos_lat0 * cos_lon0, cos_lat0 * sin_lon0, sin_lat0)

    cx = cy = SCREEN_W // 2

    # theme.h kDayPalette, as the RGB888 the RGB565 values expand to.
    BG, OCEAN, GRID, LAND, LAND_FILL = ((4, 6, 10), (8, 32, 56), (24, 48, 64),
                                        (40, 108, 80), (16, 60, 40))
    ACCENT, WARN = (80, 220, 255), (255, 68, 56)

    fb = bytearray(SCREEN_W * SCREEN_W * 3)

    def put(x, y, c):
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_W:
            o = (y * SCREEN_W + x) * 3
            fb[o:o + 3] = bytes(c)

    for i in range(SCREEN_W * SCREEN_W):
        fb[i * 3:i * 3 + 3] = bytes(BG)

    def land_at(x, y, z):
        """Must match landAt() in globe_screen.cpp exactly."""
        ax, ay, az = abs(x), abs(y), abs(z)
        if ax >= ay and ax >= az:
            m, face, sc, tc = ax, (0 if x > 0 else 1), y, z
        elif ay >= az:
            m, face, sc, tc = ay, (2 if y > 0 else 3), x, z
        else:
            m, face, sc, tc = az, (4 if z > 0 else 5), x, y
        r = 1.0 / m
        i = int((sc * r + 1.0) * 0.5 * n)
        j = int((tc * r + 1.0) * 0.5 * n)
        i = min(n - 1, max(0, i))
        j = min(n - 1, max(0, j))
        idx = face * n * n + j * n + i
        return (bits[idx >> 3] >> (idx & 7)) & 1

    # --- the disc: one land/ocean decision per pixel ---
    inv_r = 1.0 / GLOBE_R_PX
    for py in range(cy - GLOBE_R_PX, cy + GLOBE_R_PX + 1):
        nn = (cy - py) * inv_r
        for px_ in range(cx - GLOBE_R_PX, cx + GLOBE_R_PX + 1):
            e = (px_ - cx) * inv_r
            d2 = e * e + nn * nn
            if d2 > 1.0:
                continue
            u = math.sqrt(1.0 - d2)
            X = e * east[0] + nn * north[0] + u * up[0]
            Y = e * east[1] + nn * north[1] + u * up[1]
            Z = e * east[2] + nn * north[2] + u * up[2]
            put(px_, py, LAND_FILL if land_at(X, Y, Z) else OCEAN)

    def project(v):
        x, y, z = v[0] / SCALE, v[1] / SCALE, v[2] / SCALE
        e = east[0] * x + east[1] * y + east[2] * z
        nn = north[0] * x + north[1] * y + north[2] * z
        u = up[0] * x + up[1] * y + up[2] * z
        return (int(round(cx + GLOBE_R_PX * e)),
                int(round(cy - GLOBE_R_PX * nn)), u >= 0.0)

    def line(x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            put(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def draw(strokes, colour):
        for stroke in strokes:
            prev = None
            for v in stroke:
                x, y, vis = project(v)
                if vis and prev is not None:
                    line(prev[0], prev[1], x, y, colour)
                prev = (x, y) if vis else None

    draw(grat, GRID)
    draw(equator, LAND)
    draw(coast, LAND)

    ix, iy, ivis = project([c * SCALE for c in to_unit(view_lon, view_lat)])
    if ivis:
        line(ix - 7, iy, ix + 7, iy, ACCENT)
        line(ix, iy - 7, ix, iy + 7, ACCENT)
    hx, hy, hvis = project([c * SCALE for c in to_unit(-117.826, 33.746)])
    if hvis:
        for dy in (0, 1):
            for dx in (0, 1):
                put(hx + dx, hy + dy, WARN)

    # --- PNG, nearest-neighbour upscaled ---
    raw = bytearray()
    for y in range(SCREEN_W):
        row = bytearray()
        for x in range(SCREEN_W):
            row += fb[(y * SCREEN_W + x) * 3:(y * SCREEN_W + x) * 3 + 3] * scale
        for _ in range(scale):
            raw.append(0)
            raw += row

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    PREVIEW.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", SCREEN_W * scale, SCREEN_W * scale,
                                     8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tolerance-px", type=float, default=1.1,
                    help="coastline simplification tolerance, in globe pixels")
    ap.add_argument("--min-feature-px", type=float, default=2.5,
                    help="drop islands smaller than this across")
    ap.add_argument("--view", default="20,0",
                    help="preview viewpoint as lat,lon")
    ap.add_argument("--mask-n", type=int, default=128,
                    help="land mask cube face size; 6*N*N/8 bytes of flash")
    ap.add_argument("--preview-scale", type=int, default=3,
                    help="magnify the preview PNG by this much")
    ap.add_argument("--refresh", action="store_true",
                    help="re-download instead of using .cache/")
    args = ap.parse_args()

    tolerance = args.tolerance_px / GLOBE_R_PX
    min_feature = args.min_feature_px / GLOBE_R_PX

    geojson = load_geojson(args.refresh)
    coast, raw, dropped = build_coastlines(geojson, tolerance, min_feature)
    coast = quantise(coast)
    grat, equator = build_graticule()
    grat, equator = quantise(grat), quantise(equator)

    kept = sum(len(s) for s in coast)
    total = write_header(coast, grat, equator, {
        "raw": raw, "kept": kept, "strokes": len(coast),
        "tolerance_px": args.tolerance_px,
        "min_feature_px": args.min_feature_px,
    })

    print("rasterising land mask...")
    equirect = rasterise_equirect(geojson)
    bits, land_fraction = build_cube_map(equirect, args.mask_n)
    write_mask_header(bits, args.mask_n, land_fraction)

    view_lat, view_lon = (float(v) for v in args.view.split(","))
    write_preview(coast, grat, equator, bits, args.mask_n,
                  view_lat, view_lon, args.preview_scale)

    print(f"coastline: {raw} source vertices -> {kept} kept in "
          f"{len(coast)} strokes ({dropped} small features dropped)")
    print(f"graticule: {sum(len(s) for s in grat)} + "
          f"{sum(len(s) for s in equator)} equator")
    print(f"land mask: {args.mask_n}x{args.mask_n} per face, "
          f"{land_fraction:.3f} land (Earth is ~0.29)")
    print(f"{total * 6} bytes of geometry + {len(bits)} bytes of mask "
          f"= {total * 6 + len(bits)} bytes of flash, 0 bytes of RAM")
    print(f"wrote {HEADER.relative_to(ROOT)}")
    print(f"wrote {MASK_HEADER.relative_to(ROOT)}")
    print(f"wrote {PREVIEW.relative_to(ROOT)}  (open it to check the shape)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
