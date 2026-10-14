"""Load t7ghost replay recordings (rep_*.jsonl) into per-round frame lists.

Each recording row: {"f": frame, "p1": {...fields...}, "p2": {...}}; {"reset": 1, ...} rows mark
frame-counter restarts (new round). No player names exist in the data; a replay side is
identified only by (recording file, slot, character).
"""
import json
from dataclasses import dataclass, field
from pathlib import Path

CHAR_NAMES = {
    0: "Paul", 1: "Law", 2: "King", 3: "Yoshimitsu", 4: "Hwoarang", 5: "Xiaoyu", 6: "Jin", 7: "Bryan",
    8: "Heihachi", 9: "Kazuya", 10: "Steve", 11: "Jack-7", 12: "Asuka", 13: "Devil Jin", 14: "Feng",
    15: "Lili", 16: "Dragunov", 17: "Leo", 18: "Lars", 19: "Alisa", 20: "Claudio", 21: "Katarina",
    22: "Lucky Chloe", 23: "Shaheen", 24: "Josie", 25: "Gigas", 26: "Kazumi", 28: "Nina",
    29: "Master Raven", 30: "Lee", 31: "Bob", 32: "Akuma", 33: "Kuma", 34: "Panda", 35: "Eddy",
    36: "Eliza", 37: "Miguel", 43: "Geese", 44: "Noctis", 45: "Anna", 46: "Lei", 47: "Marduk",
    48: "Armor King", 49: "Julia", 50: "Negan", 51: "Zafina", 52: "Ganryu", 53: "Leroy",
    54: "Fahkumram", 55: "Kunimitsu", 56: "Lidia",
}


def char_name(cid):
    return CHAR_NAMES.get(int(cid), f"char{int(cid)}")


@dataclass
class Round:
    frames: list = field(default_factory=list)  # list of row dicts, consecutive frames


@dataclass
class Recording:
    path: Path
    rounds: list
    probes: list = field(default_factory=list)  # name-hash probe rows (hashes only, no names)

    @property
    def stage(self):
        """Stage id from the probe rows (recordings from the 2026-10-13 stage build on), else None."""
        for p in self.probes:
            if "stage" in p:
                return p["stage"]
        return None

    @property
    def chars(self):
        r = self.rounds[0].frames[0]
        return int(r["p1"]["char"]), int(r["p2"]["char"])


def load(path, min_round_frames=120):
    rounds, cur, probes = [], Round(), []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:  # half-written last line (file still recording / cut off)
                continue
            if "names_probe" in row:
                probes.append(row)
                continue
            if row.get("reset"):
                if cur.frames:
                    rounds.append(cur)
                cur = Round()
                continue
            cur.frames.append(row)
    if cur.frames:
        rounds.append(cur)
    # Loading a replay first fast-simulates every round (Replays & Tips analysis), which the ring
    # recorder samples with gaps, then plays them in real time. Keep only near-complete segments:
    # coverage = recorded frames / frame span. The trailing post-match idle segment is short.
    def coverage(r):
        span = r.frames[-1]["f"] - r.frames[0]["f"] + 1
        return len(r.frames) / span
    fast_ends = {r.frames[-1]["f"] for r in rounds if coverage(r) < 0.97}
    rounds = [r for r in rounds if len(r.frames) >= min_round_frames and coverage(r) >= 0.97]
    # every real round also has a fast-pass copy ending on the same frame; the post-match idle
    # behind the "replay over" popup does not
    if fast_ends:
        rounds = [r for r in rounds if r.frames[-1]["f"] in fast_ends]
    # the real-time pass of a round ends on the same frame number as its fast pass; if a round
    # appears twice anyway, keep the more complete copy
    best = {}
    for i, r in enumerate(rounds):
        k = r.frames[-1]["f"]
        if k not in best or len(r.frames) > len(rounds[best[k]].frames):
            best[k] = i
    rounds = [rounds[i] for i in sorted(best.values())]
    return Recording(Path(path), rounds, probes) if rounds else None


def is_practice(rec):
    """Live-harvester files also catch Practice (same game mode 2 as offline VS). Practice: match-info
    side_info[0] = 4 (VS: 1), and no KO / no round reset (health refills). First test 2026-10-14."""
    if not rec.path.name.startswith("live_"):
        return False
    info = next((p.get("side_info") for p in rec.probes if p.get("side_info")), None)
    if info and info[0] == 4:
        return True
    ko = any(min(int(f["p1"].get("hp", 1)), int(f["p2"].get("hp", 1))) <= 0 for r in rec.rounds for f in r.frames)
    return not ko and len(rec.rounds) <= 1


def load_dir(d):
    out = []
    # rep_* = replay recordings (dev build / replay harvester), live_* = live harvester; contributor
    # folders may be nested (one subfolder per contributor)
    for p in sorted(list(Path(d).rglob("rep_*.jsonl")) + list(Path(d).rglob("live_*.jsonl"))):
        try:
            rec = load(p)
        except PermissionError:  # still being recorded
            continue
        if rec and not is_practice(rec):  # live files: Practice sessions are not training data
            out.append(rec)
    return out


if __name__ == "__main__":
    import sys
    for rec in load_dir(sys.argv[1]):
        a, b = rec.chars
        print(f"{rec.path.name}: {char_name(a)} vs {char_name(b)}, {len(rec.rounds)} rounds, "
              f"{sum(len(r.frames) for r in rec.rounds)} frames")
