"""The public Ghost dataset: anonymised recordings that every profile and the frame data are built from.

Layout of a dataset folder (see docs/DATASET.md):
  manifest.tsv               one line per recording, in processing order (players are numbered by first appearance,
                             so the order is part of the data):
                             seq file kind batch date char1 char2 player1 player2 round_ends sha256
  recordings/<batch>/<name>.jsonl.gz the recording (its original rep_ / live_ name), line for line, with the identifying header rows replaced by
                             one dataset header: {"dataset": 1, "players": ["P05", "P12"], "stage": .., "mode": ..,
                             "variant": .., "build": ..}. Frame rows are untouched.
  combos/combo_*.jsonl.gz    Practice demonstrations of combo routes, punishes and taunts (no player data).

Players are pseudonyms only (P01, P02, ...). No names, name hashes, account ids or regions exist in a dataset. The
pseudonym of each side is final: same-person merges (analysis/same_players.txt) are applied when a recording is
added; later merges are applied at build time.
"""
import gzip
import hashlib
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from load import load  # noqa: E402

MANIFEST = "manifest.tsv"
COLUMNS = ["seq", "file", "kind", "batch", "date", "char1", "char2", "player1", "player2", "round_ends", "sha256"]
HEADER_KEYS = ("stage", "mode", "variant", "build")  # the only header fields a dataset recording keeps

# every field a frame row may hold; the anonymiser refuses anything else (a new field must be checked first)
ROW_KEYS = {"f", "p1", "p2", "reset", "from", "to"}
SIDE_KEYS = {"char", "move_timer", "attack_damage", "attack_type", "move_id", "damage_taken", "recovery",
             "hit_outcome", "simple_state", "stun", "throw_tech", "complex_state", "power_crush", "jump_flags",
             "cancel_window", "rage", "rage_fx", "rage_d8", "rage_dc", "rage_e4", "rage_e8", "input_dir", "raw_held",
             "x", "y", "z", "px", "py", "pz", "hp", "side", "distance", "facing", "input_counter", "input_attack",
             "startup", "startup_end"}


def is_dataset(folder):
    return (Path(folder) / MANIFEST).exists()


def pnum(label):
    """'P05' -> 5"""
    return int(label[1:])


def read_manifest(folder):
    lines = (Path(folder) / MANIFEST).read_text(encoding="utf-8").splitlines()
    rows = []
    for line in lines:
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if parts[0] == "seq":
            continue
        rows.append(dict(zip(COLUMNS, parts)))
    return rows


def load_dataset(folder):
    """[(Recording, (player number p1 side, player number p2 side), manifest row)] in processing order."""
    out = []
    for row in read_manifest(folder):
        rec = load(Path(folder) / "recordings" / row["file"])
        if rec is None:
            raise ValueError(f"{row['file']}: no usable rounds (a dataset only holds usable recordings)")
        out.append((rec, (pnum(row["player1"]), pnum(row["player2"])), row))
    return out


def round_ends(rec):
    return sorted({r.frames[-1]["f"] for r in rec.rounds})


def check_rows(lines, name):
    """Refuse a recording that holds any field outside the known gameplay schema."""
    for line in lines:
        row = json.loads(line)
        extra = set(row) - ROW_KEYS
        if extra:
            raise ValueError(f"{name}: unknown row field(s) {sorted(extra)}")
        for side in ("p1", "p2"):
            if side in row and set(row[side]) - SIDE_KEYS:
                raise ValueError(f"{name}: unknown {side} field(s) {sorted(set(row[side]) - SIDE_KEYS)}")


def anonymised_lines(raw_lines, players, name):
    """The recording's lines with every header / probe row (names_probe, side_info, ...) replaced by one dataset
    header holding only HEADER_KEYS and the pseudonyms; frame rows verbatim (checked against the schema)."""
    header = {"dataset": 1, "players": list(players)}
    frames = []
    for line in raw_lines:
        s = line.strip()
        if not s:
            continue
        try:
            row = json.loads(s)
        except json.JSONDecodeError:
            continue  # half-written last line; the loader skips it too
        if "f" in row or row.get("reset"):
            frames.append(s)
            continue
        for k in HEADER_KEYS:  # a probe / info row: keep only the plain match facts
            if k in row and k not in header:
                header[k] = row[k]
    check_rows(frames, name)
    return [json.dumps(header, separators=(",", ":"))] + frames


def write_gz(path, lines):
    data = ("\n".join(lines) + "\n").encode("utf-8")
    tmp = Path(str(path) + ".part")
    with open(tmp, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0) as gz:  # no name / time in the gzip header
            gz.write(data)
    with gzip.open(tmp, "rb") as f:
        if f.read() != data:
            raise IOError(f"{path}: read-back mismatch")
    os.replace(tmp, path)
    return hashlib.sha256(data).hexdigest()


def write_manifest(folder, rows):
    with open(Path(folder) / MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(str(r[c]) for c in COLUMNS) + "\n")


COMBO_HEADER = {"combo_lab", "mode", "stage"}


def add_combo(folder, src):
    """Copy one Practice demonstration (combo_*.jsonl[.gz]) into <dataset>/combos, line order kept. These hold no
    player data; the schema check still applies. Returns the dataset file name."""
    from recfile import open_rec
    kept, frames = [], []
    with open_rec(src) as fh:
        for line in fh:
            s = line.strip()
            if not s:
                continue
            try:
                row = json.loads(s)
            except json.JSONDecodeError:
                continue
            if "f" in row or row.get("reset"):
                frames.append(s)
            elif not set(row) <= COMBO_HEADER:
                raise ValueError(f"{src}: unexpected header {sorted(row)}")
            kept.append(s)
    check_rows(frames, Path(src).name)
    name = Path(src).name if str(src).endswith(".gz") else Path(src).name + ".gz"
    (Path(folder) / "combos").mkdir(exist_ok=True)
    write_gz(Path(folder) / "combos" / name, kept)
    return name


def write_combo_index(folder):
    """<dataset>/combos/index.tsv: file, date, P1 / P2 character, frames of every demonstration."""
    from load import char_name
    rows = ["file\tdate\tchar_p1\tchar_p2\tframes"]
    d = Path(folder) / "combos"
    for p in sorted(d.glob("combo_*.jsonl.gz")):
        c, n = None, 0
        with gzip.open(p, "rt", encoding="utf-8") as fh:
            for line in fh:
                if line.startswith('{"f"'):
                    n += 1
                    if c is None:
                        r = json.loads(line)
                        c = (r["p1"]["char"], r["p2"]["char"])
        st = p.name.split("_")[1]
        names = (char_name(c[0]), char_name(c[1])) if c else ("-", "-")
        rows.append(f"{p.name}\t{st[:4]}-{st[4:6]}-{st[6:8]}\t{names[0]}\t{names[1]}\t{n}")
    (d / "index.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
