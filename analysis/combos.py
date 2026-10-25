"""Combo lab: find the combos demonstrated in Practice recordings (Ctrl+F12, t7ghost/combos/combo_*.jsonl).

For every launch of the opponent (they become airborne) the attacker's combo is cut out: its starter (normal
hit, counter hit, or after a low parry), the launcher move, every follow-up move with its frame offset and
whether it hit, the damage, readable notation built from the inputs, and the exact inputs (player-relative
held words, run-length coded) the Ghost will replay.

  py analysis/combos.py <combo_*.jsonl or folder> [...] [--side p1|p2] [--out <candidates.txt>]

Repeated attempts of the same route are grouped, so the author never has to say what was recorded or in
which order: the list is sorted by starter and launcher, each route with how often it was done and its best
damage. Prints the routes and writes the candidates file (default: next to the first input,
combos_<char>_candidates.txt) with every attempt; the author picks which route numbers to keep.
"""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from extract import cls, rel_held, rle  # noqa: E402
from load import char_name  # noqa: E402

LEAD = 12          # frames of input kept before the launcher starts (motion inputs, buffering)
MAX_GAP = 150      # longest pause between two hits of one combo (the longest real one so far: ~130)
END_FREE = 30      # the combo is over once the opponent is out of juggle / stun this long
KEEP_MIN = 3       # a route is kept once it was done this many times 
PARRY_WINDOW = 90  # a launcher this soon after a low parry counts as a low-parry combo
BTN = [(0x4000, "1"), (0x8000, "2"), (0x1000, "3"), (0x2000, "4")]
DIRS = {0x1: "u", 0x2: "d", 0x4: "b", 0x8: "f", 0x5: "ub", 0x9: "uf", 0x6: "db", 0xA: "df"}


def segments(path):
    """Frames of one recording, split where the game's frame counter restarted (new round / reset)."""
    seg, out = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except ValueError:
                continue  # a line cut short by stopping the recording
            if "reset" in d:
                if seg:
                    out.append(seg)
                seg = []
            elif "f" in d and "p1" in d:
                seg.append(d)
    if seg:
        out.append(seg)
    return out


def damage_steps(frames, side, hp_hits=False):
    """Per frame: damage this side took on that frame. damage_taken is the last hit's damage, so two hits
    in a row with the same damage don't change it (Katarina's uf+4,4: 10 and 10); with hp_hits a drop of
    the health % counts as a hit then too (punishes.py; the approved combo routes keep the old reading)."""
    out, last = [0] * len(frames), int(frames[0][side]["damage_taken"])
    last_hp = int(frames[0][side]["hp"])
    for i, f in enumerate(frames):
        d, hp = int(f[side]["damage_taken"]), int(f[side]["hp"])
        if d != last and d > 0:
            out[i] = d
        elif hp_hits and hp < last_hp and not any(out[max(0, i - 3):i]):
            # same damage as the hit before; the health bar moved. A throw leaves damage_taken at 0 (King's
            # grab ender, 2026-10-22): its health drop (%) stands in for the damage
            out[i] = d if d > 0 else last_hp - hp
        last, last_hp = d, hp
    return out


def notation(frames, me, a, b):
    """Buttons first pressed in frames [a, b) with the direction held then, e.g. 'df+1+2'; with no
    button, a dash ('f,f') or the direction alone; '' when nothing was pressed (transitions)."""
    prev = rel_held(frames[a - 1][me]) if a > 0 else 0
    taps = []
    for i in range(a, b):
        h = rel_held(frames[i][me])
        new = h & ~prev & 0xF000
        if (h & 0xF) and (h & 0xF) != (prev & 0xF):
            taps.append(DIRS.get(h & 0xF, ""))
        prev = h
        if new:
            # buttons pressed within 2 frames count as one press (1+2 etc.)
            for j in range(i + 1, min(i + 3, b)):
                new |= rel_held(frames[j][me]) & 0xF000
            btn = "+".join(n for bit, n in BTN if new & bit)
            d = DIRS.get(h & 0xF, "")
            return f"{d}+{btn}" if d else btn
    if taps.count("f") >= 2:
        return "f,f"
    return taps[-1] if taps else ""


def combos_in(frames, me):
    op = "p2" if me == "p1" else "p1"
    n = len(frames)
    states = [cls(f[op]) for f in frames]
    dmg = damage_steps(frames, op, HP_HITS)
    # low parries: the opponent goes from attacking into a stun without taking damage
    parries = [i for i in range(1, n) if states[i] == "stun" and states[i - 1] == "attack" and dmg[i] == 0]
    found, i = [], 1
    while i < n:
        # --stun: also combos that start with a crumple-type stun (Kazuya, 2026-10-20: a stun move, then
        # a guaranteed f,f+4): our hit puts them into simple state 12 / 13 / 15 while stunned
        stun_start = (STUN_STARTS and states[i] == "stun" and int(frames[i][op]["simple_state"]) in CRUMPLE
                      and int(frames[i - 1][op]["simple_state"]) not in CRUMPLE)
        if not (states[i] == "juggled" and states[i - 1] != "juggled") and not stun_start:
            i += 1
            continue
        # the launcher: my move when the launching hit landed (the last damage on or before the launch)
        h = i
        while h > 0 and dmg[h] == 0 and i - h < 20:
            h -= 1
        low_parry = dmg[h] == 0 and states[i - 1] == "attack"  # attack -> airborne with no damage
        if low_parry:
            # the parry: my moves from the first change within the last 20 frames (the parry input)
            h, t0 = i, max(1, i - 20)
            while t0 < i and int(frames[t0][me]["move_id"]) == int(frames[t0 - 1][me]["move_id"]):
                t0 += 1
            launcher = int(frames[i][me]["move_id"])
        else:
            launcher = int(frames[h][me]["move_id"])
            t0 = h
            while t0 > 0 and int(frames[t0 - 1][me]["move_id"]) == launcher:
                t0 -= 1
        # the end: the opponent out of juggle / stun for END_FREE frames, or no hit for MAX_GAP frames (the
        # opponent lying in a knockdown before the next attempt); the combo ends at the last hit
        j, free, last_hit = i, 0, h
        while j < n and free < END_FREE and j - last_hit < MAX_GAP:
            free = free + 1 if states[j] not in ("juggled", "stun") else 0
            if dmg[j]:
                last_hit = j
            j += 1
        outcomes = {int(frames[k][op]["hit_outcome"]) for k in range(t0, min(i + 3, n))}
        parried = [p for p in parries if 0 <= t0 - p <= PARRY_WINDOW]
        starter = "stun" if stun_start else "low parry" if parried or low_parry else "counter hit" if outcomes & {10, 11} else "normal hit"
        # moves from the launcher to the last hit
        moves, k = [], t0
        while k <= last_hit:
            mv = int(frames[k][me]["move_id"])
            e = k
            while e + 1 <= last_hit and int(frames[e + 1][me]["move_id"]) == mv:
                e += 1
            # a move that ends without a hit within it but before the next hit is still part of the route
            hit = sum(dmg[k:e + 4])
            moves.append({"move": mv, "at": k - t0, "frames": e - k + 1, "dmg": hit})
            k = e + 1
        # drop pure transition states with no input change (e.g. landing frames) at the end
        while len(moves) > 1 and moves[-1]["dmg"] == 0:
            moves.pop()
        start = max(0, t0 - LEAD) if not parried else max(0, parried[-1] - 4)
        ins = [rel_held(frames[x][me]) for x in range(start, last_hit + 1)]
        notes = []
        for m in moves:
            a = t0 + m["at"]
            lo = max(start, a - 20) if not notes else t0 + prev_at + 1
            notes.append(notation(frames, me, lo, a + 1))
            prev_at = m["at"]
        if sum(1 for m in moves if m["dmg"]) < 2 - low_parry:
            i = j
            continue  # a single hit that only lifted the opponent briefly: not a combo
        # the moves that dealt the damage: the move active on each damage frame, once per use of a move
        # (a multi-hit move counts once, d+2, d+2 counts twice)
        route, uses = [], set()
        for d in range(t0, last_hit + 1):
            if dmg[d]:
                mv, u = int(frames[d][me]["move_id"]), d
                while u > t0 and int(frames[u - 1][me]["move_id"]) == mv:
                    u -= 1
                if u not in uses:
                    uses.add(u)
                    route.append(mv)
        route = tuple(route)
        # every damage frame (frames after the launcher started) and how far the opponent had been carried
        # by then, along their overall travel in the combo (walls: px/pz units, like walls.txt; the
        # direction me -> them at the launch frame jitters with the launcher's animation)
        ax, az = float(frames[t0][op]["px"]), float(frames[t0][op]["pz"])
        ux, uz = float(frames[last_hit][op]["px"]) - ax, float(frames[last_hit][op]["pz"]) - az
        ul = max(1e-3, (ux * ux + uz * uz) ** 0.5)
        hitat = [(d - t0, round(((float(frames[d][op]["px"]) - ax) * ux + (float(frames[d][op]["pz"]) - az) * uz) / ul))
                 for d in range(t0, last_hit + 1) if dmg[d]]
        found.append({
            "char": int(frames[t0][me]["char"]),
            "starter": starter,
            "launcher": launcher,
            "hits": sum(1 for m in moves if m["dmg"]),
            "damage": sum(dmg[t0:last_hit + 4]),
            "frames": last_hit - t0 + 1,
            "moves": moves,
            # the route's identity: a short move (dash step) next to a hit no longer makes attempts differ
            "route": route,
            "hitat": hitat,
            "notation": notes,
            "lead": t0 - start,
            "inputs": ins,
        })
        i = j
    return found


def main(args):
    global KEEP_ALL
    side, out = "p1", None
    global STUN_STARTS
    if "--stun" in args:
        STUN_STARTS = True
        args.remove("--stun")
    global HP_HITS, MAX_GAP
    if "--max-gap" in args:  # a longer pause between two hits (King's throw ender: grab, damage ~155 f later)
        k = args.index("--max-gap")
        MAX_GAP = int(args[k + 1])
        del args[k:k + 2]
    if "--hp-hits" in args:  # hits with the same damage as the one before (Heihachi's screw 6, Raijin 2 6, 1 6)
        HP_HITS = True
        args.remove("--hp-hits")
    if "--all" in args:  # frame-perfect demonstrations (Practice dummy playback): every route counts
        KEEP_ALL = True
        args.remove("--all")
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
    allc = []
    for fp in files:
        for seg in segments(fp):
            if len(seg) > 30:
                for c in combos_in(seg, side):
                    c["file"] = fp.name
                    allc.append(c)
    if not allc:
        print("no combos found")
        return
    for ch in sorted({c["char"] for c in allc}):
        report([c for c in allc if c["char"] == ch], out or files[0].parent)


def manual_keep():
    """(character, launcher, route) the author asked to keep anyway: analysis/combo_keep.txt. A 4th word
    "latest" also makes the route play the author's last attempt (the earlier ones were warm-ups)."""
    out, latest = set(), set()
    path = Path(__file__).with_name("combo_keep.txt")
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            parts = line.split("#")[0].split()
            if len(parts) in (3, 4):
                k = (parts[0], int(parts[1]), tuple(int(x) for x in parts[2].split(",")))
                out.add(k)
                if len(parts) == 4 and parts[3] == "latest":
                    latest.add(k)
    return out, latest


MANUAL_KEEP, MANUAL_LATEST = manual_keep()
STUN_STARTS = False  # --stun: combos may also start from a crumple-type stun
HP_HITS = False  # --hp-hits: a health-bar drop with an unchanged damage value is a hit too (2026-10-21);
                 # off by default so the approved routes read the same
CRUMPLE = {12, 13, 15}
KEEP_ALL = False  # --all: the 3-attempt rule is for human attempts; dummy playback is exact


def dropped(g, groups):
    """A route that is the start of another route with the same starter and launcher which was done at
    least twice and does more damage: the combo was cut short (dropped input), not a route of its own."""
    key, dmg = g[0]["key"], max(c["damage"] for c in g)
    return any(len(h) >= 2 and h[0]["key"][:2] == key[:2] and len(h[0]["key"][2]) > len(key[2])
               and h[0]["key"][2][:len(key[2])] == key[2] and max(c["damage"] for c in h) > dmg for h in groups)


def report(allc, out_dir):
    name = char_name(allc[0]["char"]).replace(" ", "")
    out = out_dir / f"combos_{name}_candidates.txt"
    # group the attempts: same starter, launcher and hitting moves = one route
    order = {"normal hit": 0, "counter hit": 1, "low parry": 2, "stun": 3}
    routes = {}
    for n, c in enumerate(allc, 1):
        c["n"] = n
        routes.setdefault((c["launcher"], c["route"]), []).append(c)
    # the same route counts together however it started (one attempt read as a counter hit is still the
    # same normal-hit route); it is listed under the starter most of its attempts had
    for g in routes.values():
        st = max(order, key=lambda x: sum(c["starter"] == x for c in g))
        for c in g:
            c["starter"] = st
            c["key"] = (st, c["launcher"], c["route"])
    groups = sorted(routes.values(), key=lambda g: (order[g[0]["starter"]], g[0]["launcher"] not in
                    [x["launcher"] for x in allc if x["starter"] == g[0]["starter"]][:1], g[0]["n"]))
    print(f"{name.upper()} ROUTES (repeated attempts grouped; KEEP = done {KEEP_MIN}+ times and not cut short)")
    lines, last, kept = [f"{name} ROUTES"], None, []
    for r, g in enumerate(groups, 1):
        best = max(g, key=lambda c: (c["damage"], -c["frames"]))
        if (name, best["launcher"], best["route"]) in MANUAL_LATEST:
            best = g[-1]  # the author's last attempt
        if (best["starter"], best["launcher"]) != last:
            last = (best["starter"], best["launcher"])
            title = f"== {best['starter'].upper()}  launcher {best['notation'][0] or best['launcher']} (move {best['launcher']})"
            print()
            print(title)
            lines += ["", title]
        keep = ((len(g) >= KEEP_MIN or KEEP_ALL) and not dropped(g, groups)) or (name, best["launcher"], best["route"]) in MANUAL_KEEP
        tag = "KEEP" if keep else ("cut " if len(g) >= KEEP_MIN else "    ")
        row = (f"{tag} R{r:<3} x{len(g)}  {best['hits']} hits  {best['damage']} dmg   "
               + ", ".join(x for x in best["notation"] if x) + f"   (attempt #{best['n']})")
        print(row)
        lines.append(row)
        if keep:
            kept.append((r, best, len(g)))
    lines += ["", "ALL ATTEMPTS"]
    for n, c in enumerate(allc, 1):
        head = (f"#{n:<3} {name}  {c['starter']:<11}  launcher {c['launcher']:<5} "
                f"{c['hits']} hits  {c['damage']} dmg  {c['frames']} frames  ({c['file']})")
        lines.append(head)
        lines.append("  notation: " + ", ".join(x for x in c["notation"] if x))
        lines.append("  moves: " + "  ".join(f"{m['move']}@{m['at']}{'*' if m['dmg'] else ''}" for m in c["moves"]))
        lines.append(f"  lead {c['lead']}  inputs: " + ",".join(f"{v}:{k}" for v, k in rle(c["inputs"])))
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # the kept routes: what the Ghost will play (one line per route)
    rf = out_dir / f"routes_{name}.txt"
    rl = [f"# {name} combo routes (analysis/combos.py; kept = done {KEEP_MIN}+ times, not cut short)",
          "# starter launcher hits damage frames lead | moves id@frame(*=hit) | inputs held:frames"
          " | damage frames frame:carry | notation",
          f"char {allc[0]['char']}"]
    for r, c, cnt in kept:
        rl.append(f"{c['starter'].replace(' ', '_')} {c['launcher']} {c['hits']} {c['damage']} {c['frames']} {c['lead']} | "
                  + " ".join(f"{m['move']}@{m['at']}{'*' if m['dmg'] else ''}" for m in c["moves"]) + " | "
                  + ",".join(f"{v}:{k}" for v, k in rle(c["inputs"])) + " | "
                  + ",".join(f"{d}:{w}" for d, w in c["hitat"]) + " | "
                  + ", ".join(x for x in c["notation"] if x) + f"  # R{r} x{cnt}")
    rf.write_text("\n".join(rl) + "\n", encoding="utf-8")
    print(f"\n{len(groups)} routes from {len(allc)} attempts, {len(kept)} kept -> {rf.name}  (all: {out.name})\n")


if __name__ == "__main__":
    main(sys.argv[1:])
