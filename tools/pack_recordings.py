"""Pack recordings: gzip each rep_/live_/combo_/rec_*.jsonl in the given folders (and their subfolders) into
<name>.jsonl.gz, about 13x smaller. The analysis tools read both forms (analysis/recfile.py).

  py -I tools/pack_recordings.py <folder> [...] [--dry-run] [--into <folder>]

--into packs into that folder instead of next to each file (after a harvesting session: the new live_*.jsonl of
the game's t7GhostLIVEplayData -> a new dated data folder, given LAST to build_profiles so pseudonyms stay put).
Only unpacked files are touched, so packed files already in the source folder never move.

Safe by construction: each file is compressed to <name>.jsonl.gz.part, read back and compared byte for byte with
the original, then renamed to .jsonl.gz (keeping the original's timestamp); only after that is the original
deleted. Files changed in the last 2 minutes are skipped (they may still be recording), and so is any file that
already has a packed twin with different content.
"""
import gzip
import hashlib
import os
import sys
import time
from pathlib import Path

PREFIXES = ("rep_", "live_", "combo_", "rec_")
RECENT = 120  # seconds


def pack(src, dry, into=None):
    dst = (into or src.parent) / (src.name + ".gz")
    if time.time() - src.stat().st_mtime < RECENT:
        return "skip (recent)", 0, 0
    data = src.read_bytes()
    digest = hashlib.sha256(data).digest()
    if dst.exists():
        if hashlib.sha256(gzip.decompress(dst.read_bytes())).digest() != digest:
            return "SKIP (packed twin differs)", len(data), 0
        if not dry:
            src.unlink()  # an earlier run packed it but stopped before deleting the original
        return "twin ok, original removed", len(data), dst.stat().st_size
    packed = gzip.compress(data, compresslevel=9, mtime=0)
    if hashlib.sha256(gzip.decompress(packed)).digest() != digest:
        return "SKIP (in-memory check failed)", len(data), 0
    if dry:
        return "would pack", len(data), len(packed)
    part = dst.with_name(dst.name + ".part")
    with open(part, "wb") as f:
        f.write(packed)
        f.flush()
        os.fsync(f.fileno())
    if hashlib.sha256(gzip.decompress(part.read_bytes())).digest() != digest:
        part.unlink()
        return "SKIP (written file check failed)", len(data), 0
    st = src.stat()
    os.replace(part, dst)
    os.utime(dst, (st.st_atime, st.st_mtime))
    src.unlink()
    return "packed", len(data), len(packed)


def main(argv):
    dry = "--dry-run" in argv
    argv = [a for a in argv if a != "--dry-run"]
    into = None
    if "--into" in argv:
        k = argv.index("--into")
        into = Path(argv[k + 1])
        del argv[k:k + 2]
        if not dry:
            into.mkdir(parents=True, exist_ok=True)
    folders = [Path(a) for a in argv]
    if not folders:
        print(__doc__)
        return 1
    total_in = total_out = 0
    counts = {}
    for d in folders:
        files = sorted(p for p in d.rglob("*.jsonl") if p.name.startswith(PREFIXES))
        for p in files:
            status, a, b = pack(p, dry, into)
            counts[status] = counts.get(status, 0) + 1
            total_in += a
            total_out += b
            if not status.startswith(("packed", "would pack")):
                print(f"{status}: {p}")
    for k, v in sorted(counts.items()):
        print(f"{k}: {v}")
    if total_out:
        print(f"{total_in / 1e6:.0f} MB -> {total_out / 1e6:.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
