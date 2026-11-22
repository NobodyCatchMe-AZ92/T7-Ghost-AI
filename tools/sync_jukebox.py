"""Copy Jukebox Infinity's source from its own project (the master copy) into this one (v1.4 item 12, 2026-11-22).

The standalone Jukebox Infinity keeps being released from its own project (t7-jukebox, next to this one); T7 Ghost AI builds the same code
with T7JB_EMBEDDED (build.bat). Run this after every Jukebox change, then build:

  py tools/sync_jukebox.py [<t7-jukebox folder>]

Copies src/*.hpp, src/main.cpp, src/decoders.cpp -> jukebox/src/ and third_party/dr_libs -> jukebox/third_party/dr_libs
(the standalone's proxy files stay behind: T7 Ghost AI has its own). Files that are no longer in the master are removed.
"""
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "t7-jukebox"
SKIP = {"proxy_common.hpp"}


def sync(src: Path, dst: Path, names):
    dst.mkdir(parents=True, exist_ok=True)
    want = set()
    for p in names:
        want.add(p.name)
        shutil.copy2(p, dst / p.name)
    for p in dst.iterdir():
        if p.is_file() and p.name not in want:
            p.unlink()
            print("removed", p)
    return len(want)


def main():
    if not (SRC / "src" / "main.cpp").exists():
        print("no Jukebox Infinity source at", SRC)
        return 1
    srcs = [p for p in (SRC / "src").glob("*.hpp") if p.name not in SKIP] + [SRC / "src" / "main.cpp", SRC / "src" / "decoders.cpp"]
    n = sync(SRC / "src", HERE / "jukebox" / "src", srcs)
    m = sync(SRC / "third_party" / "dr_libs", HERE / "jukebox" / "third_party" / "dr_libs",
             [p for p in (SRC / "third_party" / "dr_libs").iterdir() if p.is_file()])
    print(f"jukebox: {n} source files, {m} dr_libs files from {SRC}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
