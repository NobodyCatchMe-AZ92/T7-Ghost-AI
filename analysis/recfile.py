"""Recording files: plain `.jsonl`, or gzip-compressed `.jsonl.gz` (tools/pack_recordings.py packs them).

Everything that reads recordings goes through here, so packed and unpacked files are interchangeable. A packed
file keeps its unpacked name as its identity (`logical`): my_matches.txt, self_sides.txt and the keep lists name
recordings `live_<time>.jsonl` / `combo_<time>`, and sorting by that name keeps the processing order (and with
it the profile pseudonyms) the same whether a file is packed or not.
"""
import gzip
from pathlib import Path

GZ = ".gz"


def logical(path):
    """The recording's name without the packing suffix: live_x.jsonl.gz -> live_x.jsonl."""
    p = Path(path)
    return p.with_name(p.name[:-len(GZ)]) if p.name.endswith(GZ) else p


def open_rec(path):
    """Text reader for a recording, packed or not."""
    if str(path).endswith(GZ):
        return gzip.open(path, "rt", encoding="utf-8")
    return open(path, encoding="utf-8")


def find(folder, pattern, recursive=False):
    """Recordings matching `pattern` (e.g. "combo_*.jsonl") in `folder`, packed or not, sorted by their logical
    path. A recording present both ways (packing interrupted between writing and deleting) is listed once."""
    d = Path(folder)
    search = d.rglob if recursive else d.glob
    found = {}
    for p in list(search(pattern)) + list(search(pattern + GZ)):
        found.setdefault(logical(p), p)
    return [found[k] for k in sorted(found)]
