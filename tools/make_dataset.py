"""One-time migration: the private recording folders -> the public Ghost dataset (docs/DATASET.md).

  py -I tools/make_dataset.py --out <dataset folder> --private <private folder> --combos <t7ghost/combos> \
        --walls <t7ghost/walls_survey.txt> \
        <recordings folders, in the rebuild order>

Numbers every side exactly as analysis/build_profiles.py does for those folders (analysis.build_profiles.labelled),
then writes each used recording with its header rows (name hashes, match info) replaced by a dataset header that
holds only the pseudonyms and plain match facts. Frame rows are copied verbatim and must match the gameplay schema
(analysis/dataset.py), so a recording with any unknown field stops the run.

Into <private folder> (NEVER publish): registry.json - name hash -> pseudonym per key scope, so later intakes with
the same salt recognise returning players (tools/add_recordings.py --registry) - and origin.tsv (dataset file ->
original file) for audits.
"""
import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))
import build_profiles as bp  # noqa: E402
from dataset import add_combo, anonymised_lines, round_ends, write_combo_index, write_gz, write_manifest  # noqa: E402
from recfile import find, open_rec  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--private", required=True)
    ap.add_argument("--combos")
    ap.add_argument("--walls")  # walls_survey.txt (Practice wall laps) -> walls/walls_survey.txt.gz
    ap.add_argument("folders", nargs="+")
    a = ap.parse_args()
    out, priv = Path(a.out), Path(a.private)
    if out.exists() and any(out.iterdir()):
        sys.exit(f"{out} is not empty")
    priv.mkdir(parents=True, exist_ok=True)
    (out / "recordings").mkdir(parents=True)
    folders = [Path(f).resolve() for f in a.folders]
    batch_of = {f: f"batch{i + 1:02d}" for i, f in enumerate(folders)}

    items = bp.labelled([str(f) for f in folders])
    rows, origin, registry = [], [], {"note": "PRIVATE - name hash -> pseudonym number per key scope", "scopes": {}}
    for seq, (rec, ns) in enumerate(items, 1):
        src = next(f for f in folders if f in rec.path.resolve().parents or f == rec.path.resolve().parent)
        batch = batch_of[src]
        live = rec.path.name.startswith("live_")
        raw = find(rec.path.parent, rec.path.name)  # the packed or plain file behind the logical name
        if len(raw) != 1:
            sys.exit(f"{rec.path}: {len(raw)} files")
        with open_rec(raw[0]) as fh:
            lines = anonymised_lines(fh, [f"P{n:02d}" for n in ns], rec.path.name)
        rel = f"{batch}/{rec.path.name}.gz"
        (out / "recordings" / batch).mkdir(exist_ok=True)
        sha = write_gz(out / "recordings" / rel, lines)
        stamp = rec.path.name.split("_")[1] if "_" in rec.path.name else ""
        rows.append({"seq": seq, "file": rel, "kind": "live" if live else "replay", "batch": batch,
                     "date": f"{stamp[:4]}-{stamp[4:6]}-{stamp[6:8]}" if len(stamp) >= 8 else "-",
                     "char1": rec.chars[0], "char2": rec.chars[1], "player1": f"P{ns[0]:02d}",
                     "player2": f"P{ns[1]:02d}", "round_ends": ",".join(map(str, round_ends(rec))), "sha256": sha})
        origin.append(f"{rel}\t{raw[0]}")
        if not live:  # replays: the recorder and the opponent's keyed name hash (live hashes are never used)
            scope = "own" if src == folders[0] else batch
            me = bp.self_side(rec)
            reg = registry["scopes"].setdefault(scope, {"recorder": ns[me], "opponents": {}})
            h = bp.opponent_hash(rec)
            if h and h not in bp._PLACEHOLDERS:
                reg["opponents"][h] = ns[1 - me]
    registry["placeholders"] = sorted(bp._PLACEHOLDERS)
    write_manifest(out, rows)
    (priv / "registry.json").write_text(json.dumps(registry, indent=1), encoding="utf-8")
    (priv / "origin.tsv").write_text("\n".join(origin) + "\n", encoding="utf-8")
    print(f"{len(rows)} recordings -> {out}; players P01-P{max(max(n) for _, n in items):02d}")

    if a.combos:
        names = [add_combo(out, p) for p in find(a.combos, "combo_*.jsonl")]
        write_combo_index(out)
        print(f"{len(names)} combo recordings -> {out / 'combos'}")
    if a.walls:
        (out / "walls").mkdir()
        lines = Path(a.walls).read_text(encoding="utf-8").splitlines()
        if any(len(x.split()) != 7 for x in lines):  # <stage> <player> <px> <py> <pz> <simple> <stun>, numbers only
            sys.exit(f"{a.walls}: unexpected line format")
        write_gz(out / "walls" / "walls_survey.txt.gz", lines)
        print(f"{len(lines)} wall survey positions -> {out / 'walls'}")


if __name__ == "__main__":
    main()
