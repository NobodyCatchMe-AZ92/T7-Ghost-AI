"""Find which probed string slots hold player names, from hashes only.

Every recording carries {"names_probe": [[base, rel, enc, len, hash12], ...]} rows. A name slot
should be present in every replay; the recording player's own slot keeps one hash across all
replays while the opponent's slot changes. Static UI strings are constant everywhere and
come in no such pair. Nothing here can reveal a name: only keyed-hash prefixes exist.
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from load import load_dir, char_name  # noqa: E402


def slots_of(rec, when="f300"):
    rows = [p for p in rec.probes if p.get("when") == when] or rec.probes
    out = {}
    for row in rows:
        for base, rel, enc, ln, h in row["names_probe"]:
            out[(base, rel, enc)] = (ln, h)
    return out


def report(rep_dir):
    recs = [r for r in load_dir(rep_dir) if r.probes]
    print(f"{len(recs)} recordings with name probes")
    per_slot = collections.defaultdict(list)
    for r in recs:
        for k, v in slots_of(r).items():
            per_slot[k].append(v[1])
    n = len(recs)
    rows = []
    for k, hs in per_slot.items():
        rows.append((len(hs), len(set(hs)), k))
    rows.sort(key=lambda x: (-x[0], x[1], x[2]))
    print("present  distinct  slot")
    for present, distinct, k in rows[:80]:
        tag = "constant" if distinct == 1 else "varies" if distinct == present else ""
        print(f"{present:7} {distinct:9}  {k[0]}+0x{k[1]:X} {k[2]}  {tag}")
    for r in recs:
        a, b = r.chars
        print(f"  {r.path.name}: {char_name(a)} vs {char_name(b)}: {len(slots_of(r))} slots")


if __name__ == "__main__":
    report(sys.argv[1])
