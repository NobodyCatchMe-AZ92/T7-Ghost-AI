# Maintaining T7 Ghost AI

How to keep the mod going from this repository and the public dataset alone: add new players and characters,
record combos and punishes, change the code, and publish a release. Memory layout, file formats and how every
feature works are in [TECHNICAL.md](TECHNICAL.md); the dataset in [DATASET.md](DATASET.md).

## What you need

- Tekken 7 on Steam, final version (the mod checks the game build; addresses in the code are for that build).
- Windows, Visual Studio 2022 (C++ x64 tools), Python 3.12 (standard library only).
- This repository (code, docs, `dist/`: default ini, wall maps, learned frame data, combo routes / punishes /
  taunts) and the dataset zip from the latest GitHub release (everything the profiles and frame data are built
  from). The latest release download has the same data ready to play.

## Building

```
build.bat          -> build\dinput8.dll (dev build: recorders, Ctrl+F12 combo lab, dev hotkeys)
                      build\public\dinput8.dll (release build, T7_PUBLIC: AI, learning, settings panel only)
build_harvest.bat  -> build\harvest_replay\dinput8.dll, build\harvest_live\dinput8.dll (read-only Harvesters)
```

Install a DLL by copying it next to `TekkenGame-Win64-Shipping.exe` (`TEKKEN 7\TekkenGame\Binaries\Win64`) with the
release's `t7ghost` folder. The dev build is what you use to record; never hand it out.

## Adding new players / characters

1. **Record.**
   - Saved replays: the Replay Harvester (or the dev build) records every replay played from the Battle Log ->
     `Win64\t7GhostReplayData\rep_<time>.jsonl` (dev build: `Win64\t7ghost\replays`). Ctrl+F8 on the Battle Log
     plays the list automatically. Write `self_sides.txt` next to the files: one line `<file stem> p1|p2` per
     replay, the side the recording player had in the original match (default p1).
   - Online: the LIVE Harvester records every match played or spectated -> `Win64\t7GhostLIVEplayData\live_<time>.jsonl`
     plus `harvest_log.txt`. Write `my_matches.txt` next to the files, one line per match:
     `<file> <date> <time> <P1 char> <P2 char> <stage> <you> <left label> <right label>`
     - `you` = `P1` / `P2` (your side), `-` (spectated) or `?`.
     - labels: `.` = unknown player, a letter (`A`, `B`, ...) = the same person within this batch, `@P<n>` = an
       existing dataset player (only when you are sure).
   - Volunteered data arrives as these folders from other people; each contributor's name hashes use their own
     key, so give each contributor their own `--scope`.
2. **Pack** (optional, ~13x smaller): `py -I tools/pack_recordings.py <folder>`.
3. **Add to the dataset:** `py -I tools/add_recordings.py <dataset> <folder> --registry <private>\registry.json
   --scope <you|contributorN> [--recorder new]` (`--dry-run` first). Keep `registry.json` private: it holds name
   hashes. Back it up together with your `salt.bin`.
4. **Same person, two numbers:** add `P<new> P<old>` to `analysis/same_players.txt`. **A player who drags a Ghost
   down:** add `<Character>-P<nn>` to `analysis/excluded_profiles.txt`.
5. **Rebuild** profiles and frame data (DATASET.md) and copy them into `Win64\t7ghost` (`profiles\`,
   `framedata.txt`). The mod loads every profile in that folder; a character with at least one profile gets a Ghost.
6. Update the missing-characters list in `src/overlay.hpp` (PLEASE HELP!) and the README.

## Combo routes, punishes and taunts

Recorded in Practice with the dev build: Ctrl+F12 starts / stops a combo recording (`Win64\t7ghost\combos\combo_<time>.jsonl`).

- **Punishes:** block (or duck) the dummy's move, then punish; do each punish 3+ times. Windows are recorded per
  `analysis/punishes.py` WINDOWS (-10, -13, -15 on block; lows -11, -15). `py -I analysis/punishes.py <file>`
  writes `punishes_<char>.txt`; `analysis/punish_keep.txt` keeps / restricts single punishes (see its header).
- **Combos:** normal hit, counter hit and low parry sessions; each route done 2-3+ times.
  `py -I analysis/combos.py <file> [--hp-hits] [--max-gap 200] [--all]` writes the candidates list; pick per launcher
  the most repeated complete attempt (else the last) into `routes_<char>.txt` (`analysis/combo_keep.txt` for
  routes done fewer times).
- **Taunts:** KO the opponent, then taunt; `py -I analysis/taunts.py <file>`.
- Put the results in `Win64\t7ghost\combos` and `dist\combos`, and add the recordings to the dataset's `combos\`.
  TECHNICAL.md ("Combo routes and punishes") explains how the Ghost uses them.

## Learned frame data

`learned_framedata.txt` is written by the mod while people play (punish windows it measured). The repository keeps
the shipped copy in `dist/learned_framedata.txt`; your installed copy keeps growing as you play. For a release,
clean the installed copy and ship that: `py -I tools/release/clean_learned.py <in> <out>`, then store it in `dist/`.

## Releasing

1. Version: `T7_VERSION_STR` in `src/main.cpp` and `VERSION` in `tools/write_ini.py` (letter = new Ghost data,
   number = new features), then `py -I tools/write_ini.py dist/t7ghost.ini`. CHANGELOG.md and `dist/README.txt`.
2. `build.bat`, then check the public DLL has no dev hotkeys and no personal paths (`tools/release/pii_scan.py`).
3. Package (one zip, two folders):
   ```
   T7 Ghost Mod <ver>/  CHANGELOG.md, README.txt (dist/README.txt), THIRD_PARTY_LICENSES.txt,
                        Win64/dinput8.dll (build\public), Win64/t7ghost/ {t7ghost.ini (dist), framedata.txt, walls.txt,
                        learned_framedata.txt (cleaned), profiles/*.t7p + index.txt,
                        combos/ routes_*.txt punishes_*.txt taunts_*.txt (no *_candidates.txt)}
   Data Harvesters (for gathering and volunteering your own ghost data)/  the two Harvester DLLs + README
   ```
4. The dataset: zip it (`T7-Ghost-AI-dataset-<version>.zip`: `manifest.tsv`, `recordings/`, `combos/`, `walls/`),
   copy its `manifest.tsv` to the repository's `dataset/` and write the zip's SHA-256 to `dataset/SHA256SUMS`,
   then attach both zips to the GitHub release and tag the repository. A copy on a long-term archive (Zenodo,
   Internet Archive) keeps the data safe even if the repository goes away.
5. Rebuild check before releasing: profiles, `framedata.txt` and `walls.txt` rebuilt from the dataset must equal
   what you ship (DATASET.md).

## Rules this project keeps

- No personal information anywhere public: no names, name hashes, account ids, regions, e-mail addresses (other
  than a project contact address), real names or file paths with user names. Players are `P<nn>` only.
- Never publish or share `salt.bin` (`t7GhostHarvest_private`) or `registry.json`.
- The AI and every gameplay modifier are offline only (game mode 2); online play must never be affected. Only
  options that change nothing but your own screen (camera, lifebar HUD) may work online.
