"""Clean learned_framedata.txt before a release (the rule behind v1.0a's 26 removed readings).

A reading is how many frames the opponent stayed recovering after the Ghost blocked their move; the Ghost
uses each move's median. A 0 means they could act at once, i.e. the move is safe. When the same move also
has a reading of 10 or more it is punishable, and its 0s are measurement glitches that drag the median
down - those 0s are dropped. Moves whose readings are all below 10 are safe either way and keep their 0s.
Everything else (order, other values) is left as it is.

  py tools/release/clean_learned.py <learned_framedata.txt> <out.txt>
"""
import sys
from collections import defaultdict

PUNISHABLE = 10


def clean(lines):
    big = defaultdict(bool)
    for line in lines:
        p = line.split()
        if len(p) == 4 and p[0] == "l" and int(p[3]) >= PUNISHABLE:
            big[(p[1], p[2])] = True
    kept, dropped = [], defaultdict(int)
    for line in lines:
        p = line.split()
        if len(p) == 4 and p[0] == "l" and p[3] == "0" and big[(p[1], p[2])]:
            dropped[(p[1], p[2])] += 1
            continue
        kept.append(line)
    return kept, dropped


def main():
    src, out = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        lines = f.read().splitlines()
    kept, dropped = clean(lines)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(kept) + "\n")
    print(f"{len(lines)} readings -> {len(kept)} ({sum(dropped.values())} zeros dropped from {len(dropped)} moves)")
    for (ch, mv), n in sorted(dropped.items(), key=lambda x: (int(x[0][0]), int(x[0][1]))):
        print(f"  char {ch} move {mv}: {n}")


if __name__ == "__main__":
    main()
