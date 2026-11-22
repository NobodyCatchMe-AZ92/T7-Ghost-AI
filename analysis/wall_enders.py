"""Wall enders (v1.4 item 11, 2026-11-22): cut the author's wall combo out of combo lab recordings (Ctrl+F12 in
Practice, t7ghost/combos/combo_*.jsonl - recorded with a build that writes the "wall" / "screw" flags per frame).

How to record (author): at the wall, splat the opponent (any move that wall splats), then do the character's wall
combo; repeat a few times. The ender is everything the attacker inputs from the frame the opponent's wall splat
starts (their reaction becomes a wDm_* move) until the hits stop. The Ghost replays exactly those inputs, timed from
the splat start it sees itself.

  py analysis/wall_enders.py <combo_*.jsonl or folder> [...] [--side p1|p2] [--pick N] [--reach M] [--out combos]

Prints every attempt (splat move, ender notation, damage, length) and writes combos/walls_<Char>.txt with the best
attempt (most damage; --pick N for another one). One ender per character .
"""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from combos import damage_steps, notation, segments  # noqa: E402
from extract import cls, rel_held, rle  # noqa: E402
from load import char_name  # noqa: E402
from recfile import find  # noqa: E402

GAP = 70        # the ender is over once no hit landed for this many frames
FIRST = 120     # ...except before its first hit: a splat staggers long and the wall combo may wait it out
TAIL = 10       # frames kept after the last hit (its recovery is the Ghost's own business after that)
EARLY = 6       # a press this many frames before the splat started is moved onto the splat's first frame
RESPLAT = 20    # a splat start this soon after a splat is the same splat (hits on it restart the reaction)


def enders_in(frames, me):
    op = "p2" if me == "p1" else "p1"
    oi = 1 if me == "p1" else 0
    if not frames or "wall" not in frames[0]:
        return []
    dmg = damage_steps(frames, op, True)
    out = []
    n = len(frames)
    for s in range(1, n):
        if not (frames[s]["wall"][oi] == 1 and frames[s - 1]["wall"][oi] != 1):
            continue
        if any(frames[k]["wall"][oi] == 1 for k in range(max(0, s - RESPLAT), s - 1)):
            continue  # each hit on a splatted opponent restarts the splat for a frame: still the same splat
        # the hits after the splat, until a gap of GAP frames
        last = s
        for k in range(s, n):
            if dmg[k] > 0:
                last = k
            elif k - last > (GAP if last > s else FIRST):
                break
        if last == s:
            continue  # nothing hit after the splat: not an ender
        e = min(n, last + TAIL)
        held = [rel_held(frames[k][me]) for k in range(s, e)]
        # buttons still held from the move that splatted are not part of the ender (replayed, they would be a fresh
        # press): dropped until released
        carry = rel_held(frames[s - 1][me]) & 0xF000
        for i in range(len(held)):
            carry &= held[i]
            if not carry:
                break
            held[i] &= ~carry
        # a button pressed just before the splat started (buffered) is kept: it moves onto the first frame
        early = 0
        for k in range(max(0, s - EARLY), s):
            early |= rel_held(frames[k][me]) & ~rel_held(frames[k - 1][me] if k > 0 else frames[k][me]) & 0xF000
        if early:
            held[0] |= early
        # the wait before the first move is not kept: the Ghost starts the ender as soon as it can act after the splat
        # (the author's wait was their splat move's recovery). Kept from the direction leading into the first button.
        lead = next((i for i, h in enumerate(held) if h & 0xF000), 0)
        while lead > 0 and held[lead - 1] and (held[lead - 1] & 0xF) == (held[lead] & 0xF) and not held[lead - 1] & 0xF000:
            lead -= 1
        held = held[lead:]
        total, hits = 0, []
        for k in range(s, e):
            if dmg[k] > 0:
                total += dmg[k]
                hits.append((max(0, k - s - lead), total))
        # readable notation: one entry per move of ours that started after the splat
        names, prev_move, mark = [], frames[s][me]["move_id"], max(0, s - EARLY)
        for k in range(s + 1, e):
            mv = frames[k][me]["move_id"]
            if mv != prev_move and cls(frames[k][me]) == "attack":
                nt = notation(frames, me, mark, k + 1)
                if nt:
                    names.append(nt)
                mark = k + 1
            prev_move = mv
        splat_by = notation(frames, me, max(0, s - 40), s + 1)
        out.append({
            "char": int(frames[s][me]["char"]),
            "screw": frames[s]["screw"][oi] == -1,
            "start": s, "frames": len(held), "damage": total, "lead": lead,
            # how far apart we were when the ender's first input went in: after a screw the Ghost closes to this first
            "reach": int(frames[min(n - 1, s + lead)][me].get("distance", 0)),
            "held": held, "hits": hits,
            "notation": ", ".join(names) or "?",
            "splat_by": splat_by or "?",
        })
    return out


def main(args):
    side, pick, out_dir, paths, reach = None, None, None, [], 0
    it = iter(args)
    for a in it:
        if a == "--side":
            side = next(it)
        elif a == "--pick":
            pick = int(next(it))
        elif a == "--reach":
            # the ender's first move reaches less than the usual 2.5 m (after a screw the Ghost dashes in to this, in
            # metres): e.g. Claudio's df+3. The attempts print where the author started it from
            reach = int(float(next(it)) * 1000)
        elif a == "--out":
            out_dir = next(it)
        else:
            paths.append(a)
    files = []
    for p in paths:
        files += find(p, "combo_*.jsonl") if os.path.isdir(p) else [p]
    found = []
    for path in files:
        for seg in segments(path):
            for me in ([side] if side else ["p1", "p2"]):
                for e in enders_in(seg, me):
                    e["file"] = os.path.basename(path)
                    found.append(e)
    if not found:
        print("no wall enders found (was the recording made with a build that writes the wall flags?)")
        return 1
    by_char = {}
    for e in found:
        by_char.setdefault(e["char"], []).append(e)
    out_dir = Path(out_dir or Path(__file__).resolve().parent.parent / "combos")
    for ch, lst in by_char.items():
        name = char_name(ch)
        print(f"== {name} (char {ch}): {len(lst)} attempt(s)")
        for i, e in enumerate(lst, 1):
            print(f"  {i:2}. {e['damage']:3} dmg, {e['frames']:3} f{'  (after a screw)' if e['screw'] else ''}  "
                  f"waited {e['lead']:3} f  from {e['reach'] / 1000:.2f} m  splat by {e['splat_by']:10}  ender: {e['notation']}   [{e['file']} @{e['start']}]")
        best = lst[pick - 1] if pick else max(lst, key=lambda e: e["damage"])
        held = ",".join(f"{h}:{n}" for h, n in rle(best["held"]))
        hits = ",".join(f"{f}:{d}" for f, d in best["hits"])
        path = out_dir / f"walls_{name}.txt"
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Wall ender (analysis/wall_enders.py): inputs from the first move after the splat (the Ghost starts it as soon as it can act).\n")
            f.write(f"# {best['notation']} - splat by {best['splat_by']}, {best['damage']} dmg ({best['file']} @{best['start']})\n")
            f.write(f"char {ch}\n")
            f.write(f"ender {best['frames']} {best['damage']}{f' {reach}' if reach else ''} | {held} | {hits} | {best['notation']}\n")
        print(f"  -> {path} (attempt {lst.index(best) + 1})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
