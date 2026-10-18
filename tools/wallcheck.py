"""Check the wall survey of one stage: the arena outline and how much of each wall was walked.

  py -I tools/wallcheck.py <walls_survey.txt> <stage id> [--since HH:MM:SS line-count-ignored]

walls_survey.txt lines (written in Practice by the dev build): <stage> <player> <px> <py> <pz> <simple> <stun>

Method: main-floor positions (the most common height) -> convex outline. Each outline side is cut
into BIN-long pieces; a piece counts as walked if some position lies within NEAR of it (players
slide along walls, so a real wall is lined with positions; an outline side that only joins two far
points is not). A side with >= CONFIRM of its length walked is a confirmed wall.
Map: '.' visited, 'W' walked wall, '?' outline not walked yet (go there), top of the map = +z.
"""
import math
import sys

CELL = 100      # downsample positions to this grid
BIN = 300       # wall pieces
NEAR = 500      # a position this close to a piece walks it (root positions run ~a body width inside the wall)
CONFIRM = 0.7   # share of a side's length that must be walked
MERGE_DEG = 10  # outline sides turning less than this are one wall


def load(path, stage):
    pts = []
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) >= 5 and p[0] == str(stage):
            try:
                pts.append((float(p[2]), float(p[3]), float(p[4])))
            except ValueError:
                pass
    return pts


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
    return lo[:-1] + up[:-1]


def merge_sides(h):
    """Join consecutive outline sides that turn less than MERGE_DEG (one straight wall)."""
    def ang(a, b):
        return math.atan2(b[1] - a[1], b[0] - a[0])
    out = list(h)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            d = abs((math.degrees(ang(b, c) - ang(a, b)) + 180) % 360 - 180)
            if d < MERGE_DEG:
                out.pop(i)
                changed = True
                break
    return out


def compass(nx, nz):
    names = ["E", "NE", "N", "NW", "W", "SW", "S", "SE"]
    return names[int(round(math.atan2(nz, nx) / (math.pi / 4))) % 8]


def main():
    path, stage = sys.argv[1], int(sys.argv[2])
    pts = load(path, stage)
    if not pts:
        sys.exit(f"stage {stage}: no survey points yet")
    ys = [round(y / 200) * 200 for _, y, _ in pts]
    floor = max(set(ys), key=ys.count)
    cells = {(round(x / CELL) * CELL, round(z / CELL) * CELL) for x, y, z in pts if abs(y - floor) < 600}
    flat = list(cells)
    h = merge_sides(hull(flat))
    print(f"stage {stage}: {len(pts)} points, main floor ~{floor:.0f}, outline with {len(h)} sides")
    # index cells for proximity tests
    grid = {}
    for x, z in flat:
        grid.setdefault((int(x // 500), int(z // 500)), []).append((x, z))
    def near(x, z):
        gx, gz = int(x // 500), int(z // 500)
        for i in (-1, 0, 1):
            for j in (-1, 0, 1):
                for px, pz in grid.get((gx + i, gz + j), ()):
                    if (px - x) ** 2 + (pz - z) ** 2 <= NEAR * NEAR:
                        return True
        return False
    walked_pts, open_pts = [], []
    print("side  faces  length  walked  verdict")
    confirmed = 0
    for i in range(len(h)):
        a, b = h[i], h[(i + 1) % len(h)]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(L // BIN))
        hit = 0
        for k in range(n):
            t = (k + 0.5) / n
            x, z = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            if near(x, z):
                hit += 1
                walked_pts.append((x, z))
            else:
                open_pts.append((x, z))
        # outward normal of a counter-clockwise outline = (dy, -dx)
        nx, nz = (b[1] - a[1]), -(b[0] - a[0])
        share = hit / n
        ok = share >= CONFIRM
        confirmed += ok
        print(f"  {i + 1:2}   {compass(nx, nz):3}  {L:7.0f}   {share:4.0%}   {'WALL' if ok else 'not walked enough'}")
    print(f"{confirmed}/{len(h)} sides confirmed")
    xs, zs = [x for x, _ in flat], [z for _, z in flat]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    W, H = 64, 28
    g = [[" "] * W for _ in range(H)]
    def put(x, z, ch):
        c = min(W - 1, max(0, int((x - x0) / max(1.0, x1 - x0) * (W - 1))))
        r = min(H - 1, max(0, int((z1 - z) / max(1.0, z1 - z0) * (H - 1))))
        if ch != "." or g[r][c] == " ":
            g[r][c] = ch
    for x, z in flat:
        put(x, z, ".")
    for x, z in walked_pts:
        put(x, z, "W")
    for x, z in open_pts:
        put(x, z, "?")
    print(f"map ({x1 - x0:.0f} x {z1 - z0:.0f} units; top = +z):")
    print("+" + "-" * W + "+")
    for row in g:
        print("|" + "".join(row) + "|")
    print("+" + "-" * W + "+")
    off = [y for _, y, _ in pts if abs(y - floor) >= 600]
    if off:
        print(f"{len(off)} points off the main floor (heights {min(off):.0f}..{max(off):.0f}): other levels / breaks")


if __name__ == "__main__":
    main()
