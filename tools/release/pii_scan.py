"""Privacy scan for anything that goes public (run it on every release package and public file).

  py tools/release/pii_scan.py <file or folder> [...]

Personal terms come from the environment (Windows account, computer name, profile path) plus
T7_PII_TERMS (comma separated, e.g. a real name or email) - they are never printed; a hit shows the
term's index and a masked excerpt. Text is searched as UTF-8 and UTF-16LE (binaries too), case-insensitive.
Also flags absolute user-profile paths (X:\\Users\\...) and private data file names.
"""
import os
import re
import sys
from pathlib import Path

terms = {os.environ.get("USERNAME", ""), os.environ.get("COMPUTERNAME", ""),
         Path(os.environ.get("USERPROFILE", "")).name}
terms |= {t.strip() for t in os.environ.get("T7_PII_TERMS", "").split(",")}
for t in list(terms):
    terms |= set(t.split())  # each word of a multi-word name on its own
terms = sorted({t for t in terms if len(t) >= 4}, key=str.lower)
PATTERNS = [re.compile(re.escape(t).encode("utf-8"), re.I) for t in terms]
PATTERNS += [re.compile(re.escape(t).encode("utf-16-le"), re.I) for t in terms]
EXTRA = [re.compile(rb"[A-Za-z]:\\\\?Users\\\\?[^\\\\\x00\"']{2,}", re.I),
         re.compile(rb"salt\.bin|my_matches|opponent_\d+\.txt|self_sides", re.I)]

hits = 0
files = []
for a in sys.argv[1:]:
    p = Path(a)
    files += [f for f in p.rglob("*") if f.is_file() and ".git" not in f.parts] if p.is_dir() else [p]
for f in files:
    data = f.read_bytes()
    for i, pat in enumerate(PATTERNS):
        for m in pat.finditer(data):
            ctx = data[max(0, m.start() - 20):m.end() + 20].replace(m.group(0), b"<TERM>")
            print(f"{f}: term #{i % len(terms)} at {m.start()}: {ctx!r}")
            hits += 1
    for pat in EXTRA:
        for m in pat.finditer(data):
            print(f"{f}: pattern at {m.start()}: {data[m.start():m.end() + 10]!r}")
            hits += 1
print(f"{len(files)} files, {len(terms)} personal terms, {hits} hits")
