"""Warmup animations: convert every FBX in the author's set with convert.py (one Blender run each).

  py -I tools/warmup/batch.py <converted FBX root> <out folder> [--only <Name>,...] [--test <Name>,...] [--hands]

--hands = also the finger animations (<name>_<sit>_HandL.bin / _HandR.bin, convert.py --hands).
--native = the files are animated ON the Tekken 7 skeleton (author's "MORE CONVERTED" set): convert_native.py copies
the pose bone for bone instead of retargeting (it refuses files that are not that skeleton).
File names: <Name>_<Situation>.fbx or "<Name> <Situation>.fbx" in a folder named after the character (the folder
names the character, e.g. "Master Raven" -> MasterRaven; the file's own name may have typos, e.g. "Mardul").
Situations: Round1, Up2-0, Down0-2 (also Down-02), FinalRound (also Final), Down0-2_Up2-0 (both).

--test = a fix test: those characters plus Leo and Jin (always). Run the full batch only after the author confirms.

Output names are what src/warmup.hpp loads: <Tekken 7 name>_<Round1|Up|Down|Final>.bin. ExtraCharacter1 = Lucky Chloe,
ExtraCharacter2 = Kazumi (author, 2026-11-14); a combined "Down0-2_Up2-0" file is written as both Down and Up.
Writes <out>/batch_report.txt: per animation the frame count, the retarget's worst bone error and any failure.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

BLENDER = r"C:\Program Files\Blender Foundation\Blender 3.6\blender.exe"
HERE = Path(__file__).resolve().parent
MANNEQUIN = HERE.parents[2] / "!REFERENCES" / "TekkenMannequin_no_ik.blend"
NAMES = {"ExtraCharacter1": "LuckyChloe", "ExtraCharacter2": "Kazumi"}  # Kazumi back in (author 2026-11-15, tune by cut / blend)
SKIP = set()
# an animation used for another situation than its file name says (author 2026-11-15)
SIT_OVERRIDE = {"Josie_Round1": ["Down"]}
SITS = {"Round1": ["Round1"], "Up2-0": ["Up"], "Down0-2": ["Down"], "Down-02": ["Down"], "FinalRound": ["Final"], "Final": ["Final"],
        "Down0-2_Up2-0": ["Down", "Up"]}
NATIVE = "--native" in sys.argv

root, out = Path(sys.argv[1]), Path(sys.argv[2])
only = set(sys.argv[sys.argv.index("--only") + 1].split(",")) if "--only" in sys.argv else None
# fix tests (author, 2026-11-15): a fix is tried on a few characters first (--test <names>), and the full batch runs
# only after the author confirms it. Leo and Jin are always in a test set.
ALWAYS_TEST = {"Leo", "Jin"}
if "--test" in sys.argv:
    only = set(sys.argv[sys.argv.index("--test") + 1].split(",")) | ALWAYS_TEST
out.mkdir(parents=True, exist_ok=True)
report = []
for fbx in sorted(root.rglob("*.fbx")):
    folder = fbx.parent.name
    if folder in SKIP or (only and folder not in only):
        continue
    parts = re.split(r"[ _]", fbx.stem, maxsplit=1)
    if len(parts) < 2:
        report.append(f"{fbx.name}: SKIPPED (no situation in the name)")
        continue
    sit = parts[1].strip()
    if sit not in SITS:
        report.append(f"{fbx.name}: SKIPPED (unknown situation '{sit}')")
        continue
    name = NAMES.get(folder, folder.replace(" ", ""))
    sits = SIT_OVERRIDE.get(fbx.stem, SITS[sit])
    first = out / f"{name}_{sits[0]}.bin"
    r = subprocess.run([BLENDER, "-b", str(MANNEQUIN), "-P", str(HERE / ("convert_native.py" if NATIVE else "convert.py")), "--", str(fbx), str(first)] + (["--hands"] if "--hands" in sys.argv else []),
                       capture_output=True, text=True)
    log = r.stdout + r.stderr
    conv = re.search(r"CONVERTED (\d+) frames", log)
    err = re.search(r"MAXERR (.*)", log)
    if not conv or not first.exists():
        tail = [l for l in log.splitlines() if "Error" in l or "KeyError" in l or "Traceback" in l or "NOT THE TEKKEN" in l][-2:]
        report.append(f"{fbx.name}: FAILED {' | '.join(tail)}")
        print(report[-1], flush=True)
        continue
    worst = max(((b, float(v)) for b, v in re.findall(r"(\w+) ([\d.]+)", err.group(1))), key=lambda x: x[1]) if err else ("?", -1)
    for extra in sits[1:]:
        shutil.copyfile(first, out / f"{name}_{extra}.bin")
        for hs in ("_HandL", "_HandR"):
            if (out / f"{name}_{sits[0]}{hs}.bin").exists():
                shutil.copyfile(out / f"{name}_{sits[0]}{hs}.bin", out / f"{name}_{extra}{hs}.bin")
    how = "native (no retarget)" if NATIVE else f"worst {worst[0]} {worst[1]:.1f} deg"
    report.append(f"{fbx.name}: {' + '.join(f'{name}_{s}.bin' for s in sits)} {conv.group(1)} frames, {how}")
    print(report[-1], flush=True)
# Borrowed animations (author 2026-11-15): <target> <- <source>, body and hands, after every batch run (the sources
# are in out/ from this run or an earlier one)
COPIES = {
    "Julia_Down": "Xiaoyu_Round1",
    # "Julia_Final": "Leo_Down" - replaced by her own Final (author 2026-11-15, MORE CONVERTED set)
    "Katarina_Round1": "Nina_Up",
    "Katarina_Down": "Anna_Final",
    "Marduk_Final": "Marduk_Down",  # author 2026-11-15: his Down also as Final (his own Final in warmup-backups)
}
for dst, src in COPIES.items():
    if not (out / f"{src}.bin").exists():
        report.append(f"copy {dst} <- {src}: SOURCE MISSING")
        print(report[-1], flush=True)
        continue
    for suffix in ("", "_HandL", "_HandR"):
        if (out / f"{src}{suffix}.bin").exists():
            shutil.copyfile(out / f"{src}{suffix}.bin", out / f"{dst}{suffix}.bin")
    report.append(f"copy {dst} <- {src}")
    print(report[-1], flush=True)
# animations the author removed (deleted after the copies, so a copy can still use one as its source)
REMOVED = ["Marduk_Down"]  # author 2026-11-16: no Down warmup for Marduk (his Down lives on as his Final)
for name in REMOVED:
    for suffix in ("", "_HandL", "_HandR"):
        f = out / f"{name}{suffix}.bin"
        if f.exists():
            f.unlink()
    report.append(f"{name}: removed (author)")
# the report keeps earlier runs' lines (a partial run, e.g. --only, used to replace the whole file)
rp = out / "batch_report.txt"
done = {l.split(":", 1)[0] for l in report}
old = [l for l in rp.read_text(encoding="utf-8").splitlines() if l and l.split(":", 1)[0] not in done] if rp.exists() else []
rp.write_text("\n".join(old + report) + "\n", encoding="utf-8")
print(f"{len(report)} done")
