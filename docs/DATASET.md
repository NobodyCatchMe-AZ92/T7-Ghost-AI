# The Ghost dataset

Every Ghost profile and the shipped frame data are built from one public, anonymised dataset. Anyone can rebuild
them from it, and add new recordings to it, without any of the original (private) files.

## Privacy

- Players are pseudonyms only: `P01`, `P02`, ... A pseudonym groups one person's play across characters and
  matches (e.g. a Kazumi and a Negan that are both `P19` are the same person).
- No names, name hashes, account ids, regions or other personal information exist in the dataset. The Harvesters
  store keyed name hashes (secret key: `t7GhostHarvest_private/salt.bin`) in the header rows of a recording; the
  dataset tools replace those rows before anything is written, and refuse a recording with any field outside the
  known gameplay schema (`analysis/dataset.py`).
- The hash -> pseudonym registry (`registry.json`) and `salt.bin` stay private with whoever records. Without them
  nothing essential is lost: a returning player just gets a new number, which `analysis/same_players.txt` can merge.

## Layout

```
manifest.tsv                         one line per match recording, in processing order
recordings/<batch>/<rep_|live_...>.jsonl.gz
combos/combo_*.jsonl.gz              Practice demonstrations: combo routes, punishes, taunts (no player data)
combos/index.tsv                     file, date, characters, frames of each demonstration
walls/walls_survey.txt.gz            the Practice wall-survey laps behind walls.txt
```

Where it is: attached to the GitHub releases as `T7-Ghost-AI-dataset-<version>.zip` (unzip it anywhere). The
repository keeps its `manifest.tsv` and the zip's checksum in `dataset/`, so a copy can always be verified.

`manifest.tsv` columns (tab separated): `seq file kind batch date char1 char2 player1 player2 round_ends sha256`.
`kind` = `replay` (a saved replay played back) or `live` (an online match played or spectated); `char1/2` are game
character ids (`analysis/load.py` CHAR_NAMES); `round_ends` = the frame number each round ended on (duplicate
check); `sha256` = of the uncompressed file.

The order matters: players are numbered by first appearance, and profiles list their decisions in recording order.
Always add new batches at the end (`tools/add_recordings.py` does).

A recording is the Harvester's file line for line: one JSON object per frame
(`{"f": frame, "p1": {...}, "p2": {...}}`, fields in `src/t7.hpp` kFields), `{"reset": 1, "from": .., "to": ..}`
where the game's frame counter restarted, and one dataset header in place of the original header rows:
`{"dataset": 1, "players": ["P05", "P12"], "stage": 9, "mode": 1, "variant": "live", "build": ...}`.

## Rebuilding the shipped data

```
py -I analysis/build_profiles.py <dataset> <out>/profiles        -> <Character>-P<nn>.t7p + index.txt
py -I analysis/framedata.py      <dataset> <out>/framedata.txt
py -I analysis/build_walls.py    <dataset>/walls/walls_survey.txt.gz <out>/walls.txt
```

All three are deterministic; from the v1.2a dataset they give the v1.2a download's `profiles`, `framedata.txt`
and `walls.txt` byte for byte. `analysis/excluded_profiles.txt` (profiles left out) and `analysis/same_players.txt` (two numbers that
are one person) are applied at build time.

Punishes and taunts are rebuilt per demonstration session with `analysis/punishes.py` / `analysis/taunts.py`
(deterministic, plus `punish_keep.txt` / `taunt_keep.txt`; e.g. Eddy's punish session
`combo_20261110_173848.jsonl.gz` gives the shipped `punishes_Eddy.txt` exactly). `combos/index.tsv` tells which
character each session is for (the recording player is P1 unless `--side p2` is given). Combo routes are hand-picked from
`analysis/combos.py`'s candidates (each line of a `routes_<char>.txt` names its pick, e.g. `# R5 x5` = route 5,
done 5 times), so the shipped `dist/combos/routes_*.txt` are the source of truth for routes; the recordings are
kept so they can be re-picked or re-cut.

## Adding recordings

```
py -I tools/add_recordings.py <dataset> <folder of new rep_/live_ recordings> --registry <private registry.json>
        [--scope <key name>] [--recorder P<n>|new] [--dry-run]
```

See the tool's header and `docs/MAINTAINING.md` for the side lists it reads (`self_sides.txt`, `my_matches.txt`).
Then rebuild the profiles and frame data as above.
