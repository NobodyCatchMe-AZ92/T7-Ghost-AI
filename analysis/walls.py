"""Per-stage wall maps from observed character positions.

Positions: continuous root position px/pz (+0x1610/+0x1618) recorded every frame (recordings from
the 2026-10-13 position build on). Players get pinned against walls, so a wall shows up as a dense
pile of positions. Method, per stage:
  1. centre = mean of round-start positions (every round starts in the main arena)
  2. for each of N_DIRS directions, project all positions; walking outward from the centre, the
     first projection bin holding >= PILE_FRAC of the positions is the wall in that direction
  3. positions more than BEYOND past that wall mean the wall broke (balcony / floor break):
     the direction is marked breakable
  4. the arena = intersection of the half-planes {p : p.u <= wall(u)} (kept as N_DIRS supports)
A stage with no pile in most directions is infinite (also known: Arctic Snowfall, Infinite Azure).

walls.txt (read by the mod):
  stage <id> infinite
  stage <id> dirs <N> <d_0> ... <d_N-1>   wall support distance per direction (angle i*360/N deg),
                                          measured from the origin; -1 = no wall found that way
  stage <id> breakable <i> ...            directions whose wall broke at least once
Wall distance of a point p in unit direction u (back = away from the opponent):
  min over directions i with u.e_i > 0.3 of (d_i - p.e_i) / (u.e_i)   (ray to half-plane)
"""
import math

N_DIRS = 72
BIN = 250            # projection histogram bin width
EDGE_FRAC = 0.002   # a bin is "occupied" if it holds >= this fraction of all positions
GAP_BINS = 2         # more than this many sparse bins in a row ends the main arena
BEYOND = 1500        # this far past a wall means it broke
INFINITE = {3, 40, 55}   # Arctic Snowfall, Infinite Azure day + night (confirmed in play)


def directions(n=N_DIRS):
    return [(math.cos(2 * math.pi * i / n), math.sin(2 * math.pi * i / n)) for i in range(n)]


def build_stage(points, starts):
    """points: [(x, z)], starts: [(x, z)] round-start positions -> (supports, breakable)"""
    if not points or not starts:
        return None, set()
    cx = sum(x for x, _ in starts) / len(starts)
    cz = sum(z for _, z in starts) / len(starts)
    need_edge = max(4, int(EDGE_FRAC * len(points)))
    supports, breakable = [], set()
    for i, (ux, uz) in enumerate(directions()):
        c = cx * ux + cz * uz
        bins = {}
        far = -1e18
        for x, z in points:
            v = x * ux + z * uz
            if v > c:
                b = int((v - c) // BIN)
                bins[b] = bins.get(b, 0) + 1
                far = max(far, v)
        # the main arena = contiguous run of occupied bins outward from the centre; its outer
        # edge is the wall. A sparse gap then more positions = area behind a broken wall.
        wall = None
        b, gap = 0, 0
        while b <= max(bins, default=0):
            if bins.get(b, 0) >= need_edge:
                gap = 0
                last = b
            else:
                gap += 1
                if gap > GAP_BINS:
                    break
            b += 1
        if bins and "last" in dir():
            wall = c + (last + 1) * BIN
            del last
        supports.append(round(wall) if wall is not None else -1)
        if wall is not None and far > wall + BEYOND:
            breakable.add(i)
    return supports, breakable


def wall_distance(supports, px, pz, ux, uz):
    """Distance from (px, pz) to the wall in unit direction (ux, uz); None = no wall that way."""
    best = None
    for i, (ex, ez) in enumerate(directions(len(supports))):
        d = supports[i]
        if d < 0:
            continue
        cosang = ux * ex + uz * ez
        if cosang <= 0.3:
            continue
        t = (d - (px * ex + pz * ez)) / cosang
        if best is None or t < best:
            best = max(0.0, t)
    return best


def write(maps, path):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        for st in sorted(maps):
            m = maps[st]
            if m is None:
                f.write(f"stage {st} infinite\n")
                continue
            supports, breakable = m
            f.write(f"stage {st} dirs {len(supports)} " + " ".join(map(str, supports)) + "\n")
            if breakable:
                f.write(f"stage {st} breakable " + " ".join(map(str, sorted(breakable))) + "\n")


def read(path):
    maps = {}
    for line in open(path, encoding="ascii"):
        p = line.split()
        if len(p) < 3 or p[0] != "stage":
            continue
        st = int(p[1])
        if p[2] == "infinite":
            maps[st] = None
        elif p[2] == "dirs":
            n = int(p[3])
            maps[st] = ([int(v) for v in p[4:4 + n]], maps.get(st, ([], set()))[1] if maps.get(st) else set())
        elif p[2] == "breakable" and maps.get(st):
            maps[st] = (maps[st][0], set(int(v) for v in p[3:]))
    return maps
