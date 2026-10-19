"""Taunt lab: find the taunts demonstrated after a KO (Ctrl+F12 recordings, t7ghost/combos/combo_*.jsonl).

The author KOs the opponent (VS against a second controller is easiest) and then taunts in the time the
winner keeps control after the KO. Every KO in the recordings gives one taunt: the winner's inputs
(player-relative held words) from the KO frame until the round ends, trailing idle frames trimmed.
Taunts that are just standing still are skipped.

  py analysis/taunts.py <combo_*.jsonl or folder> [...] [--out <folder>]

Writes taunts_<char>.txt next to the first input (one line per taunt, read by the mod) and prints a
numbered list. analysis/taunt_keep.txt can drop single taunts: "-<char> <file> <ko frame>".
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from combos import segments  # noqa: E402
from extract import rel_held, rle  # noqa: E402
from load import char_name  # noqa: E402

MIN_ACTIVE = 6   # a taunt has at least this many frames with some input
MAX_FRAMES = 600  # 10 s after the KO at most


def taunts_in(frames):
    out = []
    for i in range(1, len(frames)):
        for me, op in (("p1", "p2"), ("p2", "p1")):
            if int(frames[i][op]["hp"]) <= 0 < int(frames[i - 1][op]["hp"]) and int(frames[i][me]["hp"]) > 0:
                ins = [rel_held(frames[k][me]) for k in range(i, min(len(frames), i + MAX_FRAMES))]
                while ins and ins[-1] == 0:
                    ins.pop()
                if sum(1 for h in ins if h) < MIN_ACTIVE:
                    continue
                out.append({"char": int(frames[i][me]["char"]), "ko": int(frames[i]["f"]), "inputs": ins,
                            "hp": int(frames[i][me]["hp"])})
    return out


def dropped():
    out = set()
    path = Path(__file__).with_name("taunt_keep.txt")
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            parts = line.split("#")[0].split()
            if len(parts) == 3 and parts[0].startswith("-"):
                out.add((parts[0][1:], parts[1], int(parts[2])))
    return out


def main(args):
    out = None
    if "--out" in args:
        k = args.index("--out")
        out = Path(args[k + 1])
        del args[k:k + 2]
    files = []
    for a in args:
        p = Path(a)
        files += sorted(p.glob("combo_*.jsonl")) if p.is_dir() else [p]
    found = []
    for fp in files:
        for seg in segments(fp):
            for t in taunts_in(seg):
                t["file"] = fp.stem
                found.append(t)
    drop = dropped()
    for ch in sorted({t["char"] for t in found}):
        name = char_name(ch).replace(" ", "")
        ts = [t for t in found if t["char"] == ch]
        lines = [f"# {name} taunts (analysis/taunts.py): one per line - frames | inputs held:frames  # file ko-frame",
                 f"char {ch}"]
        print(f"{name.upper()} TAUNTS")
        for n, t in enumerate(ts, 1):
            gone = (name, t["file"], t["ko"]) in drop
            print(f"  {'drop' if gone else 'KEEP'} T{n}  {len(t['inputs'])} frames, {sum(1 for h in t['inputs'] if h)} with input"
                  f"  (winner at {t['hp']}%, {t['file']} KO at frame {t['ko']})")
            if not gone:
                lines.append(f"{len(t['inputs'])} | " + ",".join(f"{v}:{k}" for v, k in rle(t["inputs"]))
                             + f"  # {t['file']} {t['ko']}")
        dest = (out or files[0].parent) / f"taunts_{name}.txt"
        dest.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"  -> {dest.name}\n")


if __name__ == "__main__":
    main(sys.argv[1:])
