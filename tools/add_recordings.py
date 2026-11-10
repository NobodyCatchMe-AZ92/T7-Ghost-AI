"""Add a batch of new recordings (Replay or LIVE Harvester output) to the Ghost dataset (docs/DATASET.md).

  py -I tools/add_recordings.py <dataset folder> <raw recordings folder> [--registry <private registry.json>]
        [--scope <name>] [--recorder P<n>|new] [--batch <name>] [--dry-run]

Every side of every new recording gets a pseudonym (P<n>), numbered on from the dataset's highest number in
processing order (replays before live, by file name) - the same rules as the original pipeline:
  * replays: the recording player's side comes from self_sides.txt in the raw folder ("<file stem> p1|p2", default
    p1); they are --recorder (default: the recorder already known for --scope in the registry, else a new number).
    The opponent is recognised by the keyed name hash in the recording: the registry maps hashes seen before to
    their pseudonym (per --scope: hashes only match when they were made with the same salt.bin). A hash seen with
    4+ different characters in the batch is the game's stand-in for an unknown name: those sides stay anonymous.
  * live matches: names are never used. my_matches.txt in the raw folder (one line per file: "<file> ... <you>
    <left label> <right label>", you = P1 / P2 / - / ?) says which side was the recording player; a label "@P<n>"
    joins existing player P<n>, a letter label groups sides of this batch as one new player, "." = anonymous.
  * anonymous sides get a number of their own (one per side).
Recordings already in the dataset (same characters and round-end frames) and Practice sessions are skipped.
Practice demonstrations (combo_*.jsonl, Ctrl+F12) in the folder go to <dataset>/combos (and combos/index.tsv).
Name hashes never enter the dataset; with --registry they are stored in that PRIVATE file only (keep it, and the
salt.bin it belongs to, out of every public place).
"""
import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))
import build_profiles as bp  # noqa: E402
from dataset import add_combo, anonymised_lines, pnum, read_manifest, round_ends, write_combo_index, write_gz, write_manifest  # noqa: E402
from load import load_dir  # noqa: E402
from recfile import find, open_rec  # noqa: E402


def is_duplicate(rec, known):
    ends = set(round_ends(rec))
    return any(c == rec.chars and len(e & ends) >= min(2, len(ends), len(e)) for c, e in known)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dataset")
    ap.add_argument("raw")
    ap.add_argument("--registry")
    ap.add_argument("--scope", default="own")
    ap.add_argument("--recorder")
    ap.add_argument("--batch")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    ds, raw = Path(a.dataset), Path(a.raw).resolve()
    rows = read_manifest(ds)
    known = [((int(r["char1"]), int(r["char2"])), {int(x) for x in r["round_ends"].split(",") if x}) for r in rows]
    used = {pnum(r[k]) for r in rows for k in ("player1", "player2")}
    registry = {"scopes": {}, "placeholders": []}
    if a.registry and Path(a.registry).exists():
        registry = json.loads(Path(a.registry).read_text(encoding="utf-8"))
    scope = registry["scopes"].setdefault(a.scope, {"recorder": None, "opponents": {}})
    used |= {n for s in registry["scopes"].values() for n in s["opponents"].values()}
    used |= {s["recorder"] for s in registry["scopes"].values() if s.get("recorder")}
    next_no = [max(used, default=0) + 1]

    def new():
        n = next_no[0]
        next_no[0] += 1
        return n

    batch = a.batch or f"batch{len({r['batch'] for r in rows}) + 1:02d}"
    recs = bp.unique_recordings(load_dir(raw))
    recs.sort(key=lambda r: (r.path.name.startswith("live_"), r.path.name))
    fresh = [r for r in recs if not is_duplicate(r, known)]
    print(f"{len(recs)} recordings, {len(recs) - len(fresh)} already in the dataset")
    # stand-in hashes: one hash over many different opponent characters
    chars = {}
    for r in fresh:
        if not r.path.name.startswith("live_"):
            h = bp.opponent_hash(r)
            if h:
                chars.setdefault(h, set()).add(r.chars[1 - bp.self_side(r)])
    placeholders = set(registry.get("placeholders", [])) | {h for h, c in chars.items() if len(c) >= bp.PLACEHOLDER_CHARS}

    recorder = [pnum(a.recorder) if a.recorder and a.recorder != "new" else
                (None if a.recorder == "new" else scope.get("recorder"))]

    def me_no():
        if recorder[0] is None:
            recorder[0] = new()
            scope["recorder"] = recorder[0]
        return recorder[0]

    letters = {}
    out_rows, seq = [], len(rows)
    for rec in fresh:
        # numbers are handed out side by side (left first), as the original pipeline numbers first appearances
        ns = [None, None]
        live = rec.path.name.startswith("live_")
        if live:
            me = bp.live_side(rec)
            labels = bp.live_labels(rec)
        else:
            me = bp.self_side(rec)
            h = bp.opponent_hash(rec)
        for i in (0, 1):
            if i == me:
                ns[i] = me_no()
            elif live and labels[i].upper().startswith("@P") and labels[i][2:].isdigit():
                ns[i] = int(labels[i][2:])
            elif live and labels[i] != ".":
                if labels[i].lower() not in letters:
                    letters[labels[i].lower()] = new()
                ns[i] = letters[labels[i].lower()]
            elif not live and h and h not in placeholders:
                if h not in scope["opponents"]:
                    scope["opponents"][h] = new()
                ns[i] = scope["opponents"][h]
            else:
                ns[i] = new()  # anonymous: a number of its own
        seq += 1
        rel = f"{batch}/{rec.path.name}.gz"
        stamp = rec.path.name.split("_")[1] if "_" in rec.path.name else ""
        row = {"seq": seq, "file": rel, "kind": "live" if live else "replay", "batch": batch,
               "date": f"{stamp[:4]}-{stamp[4:6]}-{stamp[6:8]}" if len(stamp) >= 8 else "-",
               "char1": rec.chars[0], "char2": rec.chars[1], "player1": f"P{ns[0]:02d}", "player2": f"P{ns[1]:02d}",
               "round_ends": ",".join(map(str, round_ends(rec))), "sha256": ""}
        if not a.dry_run:
            src = find(rec.path.parent, rec.path.name)[0]
            with open_rec(src) as fh:
                lines = anonymised_lines(fh, [row["player1"], row["player2"]], rec.path.name)
            (ds / "recordings" / batch).mkdir(parents=True, exist_ok=True)
            row["sha256"] = write_gz(ds / "recordings" / rel, lines)
        out_rows.append(row)
        print(f"  {row['file']}: {row['player1']} vs {row['player2']}")
    combos = find(raw, "combo_*.jsonl")  # Practice demonstrations (Ctrl+F12) in the same folder
    print(f"{len(combos)} combo recordings")
    if a.dry_run:
        print("dry run: nothing written")
        return
    for p in combos:
        add_combo(ds, p)
    if combos:
        write_combo_index(ds)
    write_manifest(ds, rows + out_rows)
    registry["placeholders"] = sorted(placeholders)
    if a.registry:
        Path(a.registry).write_text(json.dumps(registry, indent=1), encoding="utf-8")
    print(f"{len(out_rows)} recordings added as {batch}; next free pseudonym P{next_no[0]:02d}")


if __name__ == "__main__":
    main()
