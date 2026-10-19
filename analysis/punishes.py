"""Punish lab: find the punishes demonstrated in Practice recordings (Ctrl+F12, t7ghost/combos/combo_*.jsonl).

The author blocks (or ducks) a dummy's move and punishes it. For every punish the opening is found on its
own: how the opening came (block = blocked standing, low_block = blocked a low crouching, whiff = the move
whiffed over a crouch), how many frames the opponent had left (the punish window, from their recovery at
the first frame we could act), the punisher and its follow-ups (a punish that launches keeps its combo),
the damage and the exact inputs the Ghost will replay. Repeated attempts are grouped like combos.py:
kept = done KEEP_MIN+ times and not cut short, plus analysis/punish_keep.txt.

  py analysis/punishes.py <combo_*.jsonl or folder> [...] [--side p1|p2] [--out <folder>]

Writes punishes_<char>.txt (the kept punishes, read by the mod) and punishes_<char>_candidates.txt
(every attempt) next to the first input.
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from combos import END_FREE, KEEP_MIN, MAX_GAP, damage_steps, notation, segments  # noqa: E402
from extract import cls, rel_held, rle  # noqa: E402
from load import char_name  # noqa: E402

WINDOWS = {"block": [10, 13, 15], "low_block": [11, 15], "whiff": [11, 15]}  # the author's recording plan
CROUCH = {5, 6, 7}   # simple_state while crouching (brain.hpp)
LEAD = 10            # frames of input kept before the punisher's first press can matter
REACH = 20           # the punisher must start this soon after the opening


def bucket(kind, w):
    """The window class a measured window belongs to: the largest planned one it covers."""
    ok = [b for b in WINDOWS[kind] if b <= w + 1]
    return ok[-1] if ok else w


def openings(frames, me, op):
    """(frame we can act, kind, opponent move, window) for every block / low block / whiff."""
    out, n = [], len(frames)
    for i in range(1, n):
        a, b = frames[i][me], frames[i][op]
        # still in their move (its hit level is often cleared once the active frames are over)
        if int(b["move_id"]) == 32769 or cls(b) in ("stun", "juggled", "down"):
            continue
        rem = int(b["recovery"]) - int(b["move_timer"]) + 1
        # blockstun over: we were stunned by a block last frame and are not now
        pa = frames[i - 1][me]
        if int(pa["stun"]) and int(pa["hit_outcome"]) in (1, 2) and not int(a["stun"]):
            low = int(b["attack_type"]) == 1 or int(pa["simple_state"]) in CROUCH
            out.append((i, "low_block" if low else "block", int(b["move_id"]), rem))
        # whiff over a crouch: their active frames just ended, we were never hit or made to block
        se = int(b["startup_end"])
        if (se and int(b["move_timer"]) == se + 1 and int(a["simple_state"]) in CROUCH and not int(a["stun"])
                and int(b["move_id"]) == int(frames[i - 1][op]["move_id"])):
            out.append((i, "whiff", int(b["move_id"]), rem))
    return out


def punishes_in(frames, me):
    op = "p2" if me == "p1" else "p1"
    n = len(frames)
    states = [cls(f[op]) for f in frames]
    dmg = damage_steps(frames, op, hp_hits=True)
    found = []
    for i, kind, opp_move, window in openings(frames, me, op):
        if window < 8:
            continue
        # the punisher: our first move starting in reach; it must hit them while still in that move
        t0 = next((k for k in range(max(1, i - 2), min(n, i + REACH)) if int(frames[k][me]["move_id"]) !=
                   int(frames[k - 1][me]["move_id"]) and int(frames[k][me]["move_id"]) >= 1000 and
                   int(frames[k][me]["move_id"]) != 32769 and cls(frames[k][me]) == "attack"), None)
        if t0 is None:
            continue
        d0 = next((k for k in range(t0, min(n, t0 + 40)) if dmg[k]), None)
        if d0 is None or int(frames[d0 - 1][op]["move_id"]) != opp_move:
            continue  # missed, or they were already out of their move (not a punish)
        # the end: like combos.py - out of stun/juggle END_FREE frames or no hit for MAX_GAP frames
        j, free, last_hit = d0, 0, d0
        while j < n and free < END_FREE and j - last_hit < MAX_GAP:
            free = free + 1 if states[j] not in ("juggled", "stun") else 0
            if dmg[j]:
                last_hit = j
            j += 1
        # moves from the punisher to the last hit, the hitting moves (route identity), damage frames
        moves, k = [], t0
        while k <= last_hit:
            mv = int(frames[k][me]["move_id"])
            e = k
            while e + 1 <= last_hit and int(frames[e + 1][me]["move_id"]) == mv:
                e += 1
            moves.append({"move": mv, "at": k - t0, "dmg": sum(dmg[k:e + 1])})
            k = e + 1
        route, uses = [], set()
        for d in range(t0, last_hit + 1):
            if dmg[d]:
                mv, u = int(frames[d][me]["move_id"]), d
                while u > t0 and int(frames[u - 1][me]["move_id"]) == mv:
                    u -= 1
                if u not in uses:
                    uses.add(u)
                    route.append(mv)
        # inputs: from LEAD frames before the punisher; `press` = the first new button in them, the frame
        # the Ghost times to its first possible frame (the last blockstun frame / the end of the whiff)
        start = max(0, t0 - LEAD)
        ins = [rel_held(frames[x][me]) for x in range(start, last_hit + 1)]
        press, prev = 0, rel_held(frames[start - 1][me]) if start else 0
        for x, h in enumerate(ins):
            if h & ~prev & 0xF000:
                press = x
                break
            prev = h
        notes, lo = [], start
        for m in moves:
            a = t0 + m["at"]
            notes.append(notation(frames, me, lo, a + 1))
            lo = a + 1
        found.append({
            "char": int(frames[t0][me]["char"]), "kind": kind, "opp_move": opp_move, "window": window,
            # a punisher slower than the window class needs its own startup (Kazuya's 16 f ws2 on -15 lows)
            "bucket": max(bucket(kind, window), d0 - t0 + 1), "punisher": int(frames[t0][me]["move_id"]),
            "hits": len(route), "route": tuple(route), "damage": sum(dmg[t0:last_hit + 1]),
            "frames": last_hit - t0 + 1, "moves": moves, "notation": notes, "inputs": ins,
            "lead": t0 - start, "press": press, "late": t0 - i,
            "start_dist": round(float(frames[i][me]["distance"])),  # how far apart when it was started
            "launch": any(states[x] == "juggled" for x in range(d0, min(n, last_hit + 6))),
            "hitat": [d - t0 for d in range(t0, last_hit + 1) if dmg[d]],
        })
    return found


def manual_keep(drop=False, strict=False):
    """(character, kind, window class, route) the author asked to keep anyway (or, lines starting with
    '-', to drop - e.g. missed inputs that still punished): analysis/punish_keep.txt."""
    out = set()
    path = Path(__file__).with_name("punish_keep.txt")
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            parts = line.split("#")[0].split()
            if strict:
                parts = parts if len(parts) == 5 else []
            if len(parts) == 4 and parts[0].startswith("-") == drop:
                out.add((parts[0].lstrip("-"), parts[1], int(parts[2]), tuple(int(x) for x in parts[3].split(","))))
            elif strict and len(parts) == 5 and parts[4] == "strict":
                out.add((parts[0], parts[1], int(parts[2]), tuple(int(x) for x in parts[3].split(","))))
    return out


def report(allp, out_dir):
    name = char_name(allp[0]["char"]).replace(" ", "")
    groups = {}
    for n, p in enumerate(allp, 1):
        p["n"] = n
        groups.setdefault((p["kind"], p["bucket"], p["route"]), []).append(p)
    order = {"block": 0, "low_block": 1, "whiff": 2}
    gl = sorted(groups.values(), key=lambda g: (order[g[0]["kind"]], g[0]["bucket"], -max(p["damage"] for p in g)))
    keep_extra, drop, strict = manual_keep(), manual_keep(drop=True), manual_keep(strict=True)

    def cut(g):
        k, dm = (g[0]["kind"], g[0]["bucket"], g[0]["route"]), max(p["damage"] for p in g)
        return any(len(h) >= 2 and (h[0]["kind"], h[0]["bucket"]) == k[:2] and len(h[0]["route"]) > len(k[2])
                   and h[0]["route"][:len(k[2])] == k[2] and max(p["damage"] for p in h) > dm for h in gl)

    print(f"{name.upper()} PUNISHES (repeated attempts grouped; KEEP = done {KEEP_MIN}+ times and not cut short)")
    lines, kept, last = [f"{name} PUNISHES"], [], None
    for r, g in enumerate(gl, 1):
        best = max(g, key=lambda p: (p["damage"], -p["late"]))
        # the window it needs: frames from the first free frame to its hit (startup + any motion before
        # the press, e.g. EWGF 15, Asuka's ws 1,4 14) - the fastest attempt counts
        need = min(p["late"] + p["hitat"][0] + 1 for p in g)
        for p in g:
            p["need"] = need
        if (best["kind"], best["bucket"]) != last:
            last = (best["kind"], best["bucket"])
            title = f"== {best['kind'].replace('_', ' ').upper()}  -{best['bucket']} (planned class)"
            print()
            print(title)
            lines += ["", title]
        k = (name, best["kind"], best["bucket"], best["route"])
        keep = ((len(g) >= KEEP_MIN and not cut(g)) or k in keep_extra) and k not in drop
        tag = "KEEP" if keep else "drop" if k in drop else ("cut " if len(g) >= KEEP_MIN else "    ")
        wins = sorted({p["window"] for p in g})
        row = (f"{tag} P{r:<3} x{len(g)}  needs {need:<2} {best['hits']} hits  {best['damage']} dmg{' launch' if best['launch'] else ''}   "
               + ", ".join(x for x in best["notation"] if x)
               + f"   (window {'/'.join(map(str, wins))}, vs move {best['opp_move']}, attempt #{best['n']})")
        print(row)
        lines.append(row)
        if keep:
            kept.append((r, best, len(g)))
    lines += ["", "ALL ATTEMPTS"]
    for p in allp:
        lines.append(f"#{p['n']:<3} {p['kind']:<9} window {p['window']:<3} -> -{p['bucket']}  punisher {p['punisher']}  "
                     f"{p['hits']} hits  {p['damage']} dmg  started {p['late']} f after the opening  ({p['file']})")
        lines.append("  notation: " + ", ".join(x for x in p["notation"] if x))
        lines.append("  moves: " + "  ".join(f"{m['move']}@{m['at']}{'*' if m['dmg'] else ''}" for m in p["moves"]))
        lines.append(f"  lead {p['lead']} press {p['press']}  inputs: " + ",".join(f"{v}:{k}" for v, k in rle(p["inputs"])))
    (out_dir / f"punishes_{name}_candidates.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    rf = out_dir / f"punishes_{name}.txt"
    rl = [f"# {name} punishes (analysis/punishes.py; kept = done {KEEP_MIN}+ times, not cut short)",
          "# kind needed_window punisher hits damage frames lead press reach open launches | moves id@frame(*=hit) | inputs held:frames"
          " | damage frames | notation",
          f"char {allp[0]['char']}"]
    written = set()
    for r, p, cnt in kept:
        if (p["kind"], p["need"], p["route"]) in written:
            continue  # the same punish kept under two planned classes (Feng's b+1+2 at -13 and -15)
        written.add((p["kind"], p["need"], p["route"]))
        reach = max(q["start_dist"] for q in groups[(p["kind"], p["bucket"], p["route"])])
        if (name, p["kind"], p["bucket"], p["route"]) in strict:
            reach = -reach  # negative = a hard limit: never from farther than the author did it
        rl.append(f"{p['kind']} {p['need']} {p['punisher']} {p['hits']} {p['damage']} {p['frames']} {p['lead']} {p['press']} {reach} {p['lead'] - p['late']} {int(p['launch'])} | "
                  + " ".join(f"{m['move']}@{m['at']}{'*' if m['dmg'] else ''}" for m in p["moves"]) + " | "
                  + ",".join(f"{v}:{k}" for v, k in rle(p["inputs"])) + " | "
                  + ",".join(map(str, p["hitat"])) + " | "
                  + ", ".join(x for x in p["notation"] if x) + f"  # P{r} x{cnt}")
    rf.write_text("\n".join(rl) + "\n", encoding="utf-8")
    print(f"\n{len(gl)} punishes from {len(allp)} attempts, {len(kept)} kept -> {rf.name}\n")


def main(args):
    side, out = "p1", None
    if "--side" in args:
        k = args.index("--side")
        side = args[k + 1]
        del args[k:k + 2]
    if "--out" in args:
        k = args.index("--out")
        out = Path(args[k + 1])
        del args[k:k + 2]
    files = []
    for a in args:
        p = Path(a)
        files += sorted(p.glob("combo_*.jsonl")) if p.is_dir() else [p]
    allp = []
    for fp in files:
        for seg in segments(fp):
            if len(seg) > 30:
                for p in punishes_in(seg, side):
                    p["file"] = fp.name
                    allp.append(p)
    if not allp:
        print("no punishes found")
        return
    for ch in sorted({p["char"] for p in allp}):
        report([p for p in allp if p["char"] == ch], out or files[0].parent)


if __name__ == "__main__":
    main(sys.argv[1:])
