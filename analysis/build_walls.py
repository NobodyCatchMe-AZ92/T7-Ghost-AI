"""Build t7ghost/walls.txt (per-stage wall maps for the AI) from the Practice wall-survey laps.

  py -I analysis/build_walls.py <walls_survey.txt> <walls.txt out>

Every stage was lapped along all its walls (2026-10-17/18; ranges below = walls_survey.txt line
numbers, 1-based, inclusive). For each level (height band) and phase (before / after each wall
break), the outline is the convex hull of dense position cells (stray fall points dropped), in root
coordinates: distance 0 = a fighter pinned against that wall.

walls.txt:
  infinite <stage>
  alias <stage> <stage with the same layout>
  level <stage> <level> <floor height> <breakable floor 0/1>
  phase <stage> <level> <phase> <n> x z x z ...      outline, counter-clockwise
  edge  <stage> <level> <phase> <edge index> <type>  type: balcony | breakable (default: wall)
The AI uses the first phase of a level that contains every position seen on it this round, so a
break is recognised the moment someone is seen beyond the old wall.
"""
import math
import statistics
import sys

CELL = 250
MIN_CELL = 3  # positions per cell; fall streaks and stray points are thinner

# stage: [(level, y_lo, y_hi, line_lo, line_hi, phases, edge marks, breakable floor)]
#   phases: [(name, clip)]; clip = dict of x_min/x_max/z_min/z_max bounds (None = all points)
#   edge marks: {(phase, side): type}; side = outward-facing compass N/E/S/W (axis-aligned walls)
ALL = [("all", None)]
LEVELS = {
    0: [("floor", -2000, 3000, 254772, 267451, ALL, {}, 0)],
    1: [("top", 27000, 30000, 268102, 287973, ALL, {}, 1),
        ("floor2", 20000, 23000, 268102, 287973, ALL, {}, 1),   # only checked: reuses the top outline
        ("floor3", 13000, 16000, 268102, 287973, ALL, {}, 1),
        ("bottom", -1000, 2500, 268102, 287973, ALL, {}, 0)],
    2: [("upper", 6000, 9500, 287974, 306863, [("all", {"z_max": 7600})], {("all", "N"): "balcony"}, 0),
        ("lower", -2000, 3000, 287974, 306863, [("intact", {"z_min": 8000}), ("broken", None)],
         {("intact", "S"): "breakable"}, 0)],
    4: [("upper", 6000, 9500, 306864, 321683, [("all", {"z_min": -9800})], {("all", "S"): "balcony"}, 0),
        ("lower", -2000, 3000, 306864, 321683, ALL, {}, 0)],
    5: [("floor", -2000, 3000, 321684, 328914, ALL, {}, 0)],
    6: [("floor", -2000, 3000, 328915, 336521, ALL, {}, 0)],
    7: [("floor", 48000, 53000, 336522, 344126, ALL, {}, 0)],
    8: [("floor", -2000, 3000, 344127, 352812, ALL, {}, 0)],
    9: [("floor", -2000, 3000, 352813, 362077, ALL, {}, 0)],
    30: [("upper", 6000, 9500, 362078, 375685, [("all", {"x_min": -10300})], {("all", "W"): "balcony"}, 0),
         ("lower", -2000, 3000, 362078, 375685, ALL, {}, 0)],
    31: [("floor", -2000, 3000, 375686, 381880, ALL, {}, 0)],
    32: [("floor", -2000, 3000, 381881, 394156,
          [("intact", {"z_min": -11900, "z_max": 11900}), ("south_open", {"z_max": 11900}),
           ("north_open", {"z_min": -11900}), ("both_open", None)],
          {("intact", "S"): "breakable", ("intact", "N"): "breakable",
           ("south_open", "N"): "breakable", ("north_open", "S"): "breakable"}, 0)],
    35: [("floor", -2000, 3000, 394157, 402236, ALL, {}, 0)],
    37: [("floor", -2000, 3000, 407750, 414927, ALL, {}, 0)],
    39: [("floor", -2000, 3000, 414928, 422125, ALL, {}, 0)],
    51: [("upper", 6000, 9500, 431028, 456292,
          [("intact", {"x_min": -7300}), ("one_open", {"x_min": -16300}),
           ("two_open", {"x_min": -25300}), ("three_open", {"x_min": -34300})],
          {("intact", "W"): "breakable", ("one_open", "W"): "breakable",
           ("two_open", "W"): "breakable", ("three_open", "W"): "balcony"}, 0),
         ("lower", -2000, 3000, 431028, 456292, ALL, {}, 0)],
    52: [("floor", -2000, 3000, 456293, 463121, ALL, {}, 0)],
    56: [("floor", -2000, 3000, 463122, 469683, ALL, {}, 0)],
    57: [("top", 29000, 32500, 469684, 492385, [("all", {"x_min": 4300})], {("all", "W"): "balcony"}, 0),
         ("mid", 22000, 26000, 469684, 492385, ALL, {}, 1),
         ("bottom", -2000, 3000, 469684, 492385, ALL, {}, 0)],
    58: [("upper", 6000, 9500, 492386, 504808, [("all", {"x_min": 7300})], {("all", "W"): "balcony"}, 0),
         ("lower", -2000, 3000, 492386, 504808, ALL, {}, 0)],
    59: [("upper", 5000, 12000, 504809, 518577, [("all", {"x_max": -8000})], {("all", "E"): "balcony"}, 0),
         ("lower", -3000, 3500, 504809, 518577, ALL, {}, 0)],
}
ALIASES = {54: 4, 53: 2, 33: 32, 36: 35, 41: 39}
INFINITE = [3, 40, 55]
REUSE_TOP = {(1, "floor2"): "top", (1, "floor3"): "top"}  # identical floors, only spot-checked


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]  # counter-clockwise


def simplify(h, deg=4.0):
    out = list(h)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            a1 = math.atan2(b[1] - a[1], b[0] - a[0])
            a2 = math.atan2(c[1] - b[1], c[0] - b[0])
            if abs((math.degrees(a2 - a1) + 180) % 360 - 180) < deg:
                out.pop(i)
                changed = True
                break
    return out


def side_of(a, b):
    """Outward compass side of a counter-clockwise outline edge a->b (normal = (dz, -dx))."""
    nx, nz = b[1] - a[1], -(b[0] - a[0])
    ang = math.degrees(math.atan2(nz, nx)) % 360
    for name, c in (("E", 0), ("N", 90), ("W", 180), ("S", 270)):
        if abs((ang - c + 180) % 360 - 180) <= 30:
            return name
    return None


def clip_ok(x, z, c):
    if not c:
        return True
    return (x >= c.get("x_min", -1e9) and x <= c.get("x_max", 1e9) and
            z >= c.get("z_min", -1e9) and z <= c.get("z_max", 1e9))


def main(survey, out):
    lines = open(survey, encoding="utf-8", errors="replace").read().splitlines()
    res = ["# t7ghost wall maps (analysis/build_walls.py from the Practice wall-survey laps)",
           "# units: game root coordinates (~1000 per metre)"]
    res += [f"infinite {s}" for s in INFINITE]
    res += [f"alias {a} {b}" for a, b in sorted(ALIASES.items())]
    built = {}
    for stage, levels in LEVELS.items():
        for (lv, y0, y1, l0, l1, phases, marks, bfloor) in levels:
            rows = []
            for ln in lines[l0 - 1:l1]:
                p = ln.split()
                if len(p) >= 5 and p[0] == str(stage):
                    x, y, z = float(p[2]), float(p[3]), float(p[4])
                    if y0 <= y < y1:
                        rows.append((x, y, z))
            if not rows:
                print(f"stage {stage} {lv}: NO POINTS")
                continue
            floor = statistics.median(y for _, y, _ in rows)
            res.append(f"level {stage} {lv} {floor:.0f} {bfloor}")
            for pname, clip in phases:
                src = (stage, REUSE_TOP[(stage, lv)]) if (stage, lv) in REUSE_TOP else None
                if src and src in built:
                    poly = built[src]
                else:
                    cells = {}
                    for x, _, z in rows:
                        if clip_ok(x, z, clip):
                            k = (int(math.floor(x / CELL)), int(math.floor(z / CELL)))
                            cells.setdefault(k, []).append((x, z))
                    pts = [p for v in cells.values() if len(v) >= MIN_CELL for p in v]
                    poly = simplify(hull([(round(x), round(z)) for x, z in pts]))
                    built[(stage, lv)] = built.get((stage, lv), poly)
                res.append(f"phase {stage} {lv} {pname} {len(poly)} " + " ".join(f"{x} {z}" for x, z in poly))
                xs, zs = [p[0] for p in poly], [p[1] for p in poly]
                tags = []
                for i in range(len(poly)):
                    sd = side_of(poly[i], poly[(i + 1) % len(poly)])
                    t = marks.get((pname, sd))
                    if t:
                        res.append(f"edge {stage} {lv} {pname} {i} {t}")
                        tags.append(f"{sd}:{t}")
                print(f"stage {stage:2} {lv:7} {pname:10} floor {floor:6.0f}  {(max(xs) - min(xs)) / 1000:4.1f} x "
                      f"{(max(zs) - min(zs)) / 1000:4.1f}k  {len(poly):2} sides  {' '.join(tags)}")
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(res) + "\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
