"""Frame data learned from the replays: for every (character, move id) seen, measure

  startup    attack frames until it hits (the game's own startup field at move start)
  level      hit level (attack_type: 1 low, 2 mid, 4 special mid, 5 high, 10 throw, ...)
  block_adv  frame advantage for the attacker when the move is blocked:
             (defender leaves blockstun) - (attacker becomes free). -12 = defender may punish
             with any move of startup <= 12. Only samples where the attacker did NOT continue
             into another attack (string follow-ups are measured as their own moves).
  hit_adv    same on a normal (non-launching) hit
  launch     share of hits that juggled the defender
  damage     median damage per hit

framedata.txt (read by the mod):
  m <char> <move> <startup> <level> <damage> <block_adv|NA> <hit_adv|NA> <launch 0..100> <n_block> <n_hit>
"""
import collections
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(__file__))
from extract import cls  # noqa: E402

BLOCKED = (1, 2)
LAUNCH = (3, 4, 5, 7)


def first(frames, start, pred, limit=120):
    for k in range(start, min(len(frames), start + limit)):
        if pred(frames[k]):
            return k
    return None


def measure_round(frames, acc):
    n = len(frames)
    for att, dfn in (("p1", "p2"), ("p2", "p1")):
        prev_out = 0
        for i in range(1, n):
            d = frames[i][dfn]
            out = int(d["hit_outcome"])
            a = frames[i][att]
            if out == prev_out or out == 0:
                prev_out = out
                continue
            prev_out = out
            move = int(a["move_id"])
            key = (int(a["char"]), move)
            rec = acc[key]
            # which attack frame landed: the attacker's move started earlier; its startup field
            # is recorded per frame, keep the value at the move's first frame
            s = i
            while s > 0 and int(frames[s - 1][att]["move_id"]) == move:
                s -= 1
            if int(frames[s][att]["startup"]) > 0:
                rec["startup"].append(int(frames[s][att]["startup"]))
                rec["level"].append(int(frames[s][att]["attack_type"]))
            rec["dist"].append(float(a["distance"]))  # how far away it connected (hit or block)
            rec["sdist"].append(float(frames[s][att]["distance"]))  # ...and how far away it was started
            dmg = int(d["damage_taken"])
            if dmg > 0:
                rec["damage"].append(dmg)
            # attacker's next state: free (measurable) or another attack (string -> skip)
            ta = first(frames, i, lambda f: cls(f[att]) != "attack" or int(f[att]["move_id"]) != move)
            if ta is None or cls(frames[ta][att]) == "attack":
                continue
            ta = first(frames, ta, lambda f: cls(f[att]) == "free")
            td = first(frames, i, lambda f: cls(f[dfn]) not in ("stun",))
            if ta is None or td is None:
                continue
            launched = any(cls(frames[k][dfn]) == "juggled" for k in range(i, min(n, i + 15)))
            # the defender must not be hit/blocked again before leaving stun (that spans two hits)
            again = any(int(frames[k][dfn]["hit_outcome"]) not in (0, out) for k in range(i + 1, min(n, td + 1)))
            adv = td - ta
            if not again and -40 <= adv <= 15:
                if out in BLOCKED:
                    rec["block"].append(adv)
                elif not launched and out >= 10:
                    rec["hit"].append(adv)
            if out not in BLOCKED:
                rec["hits"] += 1
                rec["launches"] += launched


def build(recordings):
    acc = collections.defaultdict(lambda: {"startup": [], "level": [], "damage": [], "block": [], "hit": [],
                                           "hits": 0, "launches": 0, "dist": [], "sdist": []})
    for rec in recordings:
        for r in rec.rounds:
            measure_round(r.frames, acc)
    table = {}
    for (ch, mv), r in acc.items():
        if not r["startup"]:
            continue
        table[(ch, mv)] = {
            "startup": int(statistics.median(r["startup"])),
            "level": collections.Counter(r["level"]).most_common(1)[0][0],
            "damage": int(statistics.median(r["damage"])) if r["damage"] else 0,
            "block_adv": int(statistics.median(r["block"])) if r["block"] else None,
            "hit_adv": int(statistics.median(r["hit"])) if r["hit"] else None,
            "launch": round(100 * r["launches"] / r["hits"]) if r["hits"] else 0,
            "n_block": len(r["block"]), "n_hit": len(r["hit"]),
            # reach: 98th percentile of the distances it connected at (near-max reach)
            "range": int(sorted(r["dist"])[int(0.98 * (len(r["dist"]) - 1))]) if r["dist"] else 0,
            # start reach: the farthest it was ever started from and still connected (moves travel, so
            # this is what predicts a whiff the moment a move starts); needs 5 samples
            "start_range": int(max(r["sdist"])) if len(r["sdist"]) >= 5 else 0,
        }
    return table


def write(table, path):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("# m char move startup level damage block_adv hit_adv launch% n_block n_hit range start_range\n")
        for (ch, mv), t in sorted(table.items()):
            ba = "NA" if t["block_adv"] is None else t["block_adv"]
            ha = "NA" if t["hit_adv"] is None else t["hit_adv"]
            f.write(f"m {ch} {mv} {t['startup']} {t['level']} {t['damage']} {ba} {ha} {t['launch']} {t['n_block']} {t['n_hit']} {t['range']} {t['start_range']}\n")


if __name__ == "__main__":
    from load import load_dir
    from build_profiles import unique_recordings
    rep_dir, out = sys.argv[1], sys.argv[2]
    recs = unique_recordings(load_dir(rep_dir))
    table = build(recs)
    write(table, out)
    print(f"{len(table)} moves over {len({c for c, _ in table})} characters -> {out}")
