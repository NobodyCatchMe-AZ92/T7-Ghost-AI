"""Build per-player profiles (.t7p) from replay recordings.

One profile per (player, character), merged over every replay of that player with that
character, named "<Character>-P<nn>" (nn = player number by first appearance). Players are
identified only by in-game keyed name hashes (see src/names.hpp, analysis/identity.py);
re-watched duplicates of a match are merged. Names and hashes are never written here.
The recording player is "self" (one side of every Battle Log replay); opponents are keyed by
the hash of the match structure's opponent-name slot. Usage: build_profiles.py <replays> <out>

.t7p text format (one record per line, parsed by the mod):
  T7P 4
  char <id>
  name <pseudonym>
  frames <frames of play this profile was built from>
  k <move> <cont% whiff> <cont% blocked> <cont% hit> <n> <n> <n>   string continuation (hit-confirm)
  c <kind> <sit> <dist> <adv> <oppatk> <startup> <lead> <outcome> <damage> <first_move>
    <life> <rage> <opp_rage> <crouch> <advb> <inputs>      life: 0 behind, 1 even, 2 ahead; rage flags 0/1
     inputs = comma-separated held:count runs, e.g. 0:3,8:2,16386:1
Also writes profiles/index.txt: "<char id> <pseudonym> <frames>" sorted best-first per character.
"""
import collections
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from extract import extract_side, rle, throw_breaks  # noqa: E402
from load import load_dir, char_name  # noqa: E402

SIT = ["neutral", "opp_startup", "opp_recovery", "opp_stunned", "opp_juggled", "opp_down", "self_down"]
DIST = ["close", "mid", "far", "vfar"]
OUT = ["none", "whiff", "blocked", "hit", "counter", "juggle", "interrupted"]
KIND = ["attack", "neutral", "wakeup", "air"]
LIFE = ["behind", "even", "ahead"]


def write_profile(path, cid, name, frames, chunks, tb=(0, 0)):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("T7P 4\n")
        f.write(f"char {cid}\nname {name}\nframes {frames}\n")
        # throw breaks as the defender: tb <grabs> <breaks>
        f.write(f"tb {tb[0]} {tb[1]}\n")
        # hit-confirm table: after a string starter's first move, how often this player went on,
        # by how that first move went. k <move> <cont% whiff> <cont% blocked> <cont% hit> <n w> <n b> <n h>
        stats = {}
        for c in chunks:
            if c.kind == "attack" and c.moves:
                st = stats.setdefault(c.moves[0], {"whiff": [0, 0], "blocked": [0, 0], "hit": [0, 0]})
                st[c.first_result][0] += 1
                st[c.first_result][1] += c.continued
        for mv, st in sorted(stats.items()):
            if not any(st[r][1] for r in st):
                continue  # never continued: single moves need no confirm data
            pct = [round(100 * st[r][1] / st[r][0]) if st[r][0] else -1 for r in ("whiff", "blocked", "hit")]
            f.write(f"k {mv} {pct[0]} {pct[1]} {pct[2]} {st['whiff'][0]} {st['blocked'][0]} {st['hit'][0]}\n")
        for c in chunks:
            ins = ",".join(f"{v}:{n}" for v, n in rle(c.inputs))
            f.write(f"c {KIND.index(c.kind)} {SIT.index(c.sit)} {DIST.index(c.dist)} {c.adv} {c.oppatk} "
                    f"{c.startup} {c.lead} {OUT.index(c.outcome)} {c.damage} {c.moves[0] if c.moves else 0} "
                    f"{LIFE.index(c.life)} {int(c.rage)} {int(c.opp_rage)} {int(c.crouch)} {c.advb} {ins}\n")


def unique_recordings(recs):
    """Copies of one match (re-watches, interrupted runs) share characters and exact round-end
    frame numbers (replays are deterministic). Cluster them and keep the best copy: one with
    name-hash probes first, then the most recorded frames."""
    # Two shared round ends are required (one, if a copy has a single round): different matches of
    # the same characters can share one end frame by chance (contributors replay-harvesting loops
    # through their list more than once, so copies are expected).
    clusters = []
    for r in recs:
        ends = {x.frames[-1]["f"] for x in r.rounds}
        for c in clusters:
            if c["chars"] == r.chars and len(c["ends"] & ends) >= min(2, len(ends), len(c["ends"])):
                c["members"].append(r)
                c["ends"] |= ends
                break
        else:
            clusters.append({"chars": r.chars, "ends": set(ends), "members": [r]})
    best = []
    for c in clusters:
        best.append(max(c["members"], key=lambda r: (bool(r.probes), sum(len(x.frames) for x in r.rounds))))
    return best


OPPONENT_SLOT = ("match", 0x11C, "8")  # online opponent name slot, keyed hash only


_SELF = {}  # recordings folder -> {recording stem: own side}


def self_side(rec):
    """Index (0 = p1, 1 = p2) of the recording player's own side. The slot follows the original
    match side, not the Battle Log card or the playback side, so it comes from self_sides.txt
    (one line per replay, written by whoever harvested them) next to the replays folder, or inside a
    contributor's folder; P1 if unlisted. One list per recordings folder."""
    d = rec.path.parent
    if d not in _SELF:
        sides = {}
        for path in (d / "self_sides.txt", d.parent / "self_sides.txt"):
            if path.exists():
                for line in path.read_text().splitlines():
                    parts = line.split("#")[0].split()
                    if len(parts) == 2:
                        sides[parts[0]] = 0 if parts[1] == "p1" else 1
                break
        _SELF[d] = sides
    return _SELF[d].get(rec.path.stem, 0)


PLACEHOLDER_CHARS = 4  # a name hash seen with this many different characters is not one player


def opponent_hash(rec):
    from identity import slots_of
    return slots_of(rec).get(OPPONENT_SLOT, (0, None))[1] if rec.probes else None


_LIVE = {}  # live recordings dir -> {file name: "P1" | "P2" | "-" | "?"}


def live_side(rec):
    """Live (online) matches: which side the recording player was, from my_matches.txt next to the
    files (the player's own tick-off list: P1, P2, '-' = spectated, '?' = unsure). None if not theirs."""
    d = rec.path.parent
    if d not in _LIVE:
        _LIVE[d] = {}
        f = d / "my_matches.txt"
        if f.exists():
            for line in f.read_text(encoding="utf-8").splitlines():
                parts = line.split("#")[0].split()
                if len(parts) < 2 or not parts[0].startswith("live_"):
                    continue
                if parts[-1].upper() in ("P1", "P2", "-", "?"):  # older list without label columns
                    _LIVE[d][parts[0]] = (parts[-1].upper(), ".", ".")
                elif len(parts) >= 4:
                    _LIVE[d][parts[0]] = (parts[-3].upper(), parts[-2], parts[-1])
    v = _LIVE[d].get(rec.path.name, ("?", ".", "."))[0]
    return 0 if v == "P1" else 1 if v == "P2" else None


def live_labels(rec):
    """The player's own same-person tags for the left/right fighter of a live match ('.' = none)."""
    live_side(rec)  # loads the list
    _, a, b = _LIVE[rec.path.parent].get(rec.path.name, ("?", ".", "."))
    return a, b


def placeholder_hashes(recs):
    """Hashes that are the game's stand-in when the opponent's name is unavailable: one hash over
    many different opponent characters (one such hash covered 8 characters in 10 matches)."""
    chars = {}
    for r in recs:
        if r.path.name.startswith("live_"):
            continue
        h = opponent_hash(r)
        if h:
            chars.setdefault(h, set()).add(r.chars[1 - self_side(r)])
    return {h for h, c in chars.items() if len(c) >= PLACEHOLDER_CHARS}


_PLACEHOLDERS = set()


def player_ids(rec):
    """('self', opponent hash) in slot order. Unknown or placeholder opponent hash -> None.
    Live matches: 'self' only on the side the player ticked; everyone else stays anonymous (live
    name codes are not used to identify or group anyone)."""
    if rec.path.name.startswith("live_"):
        # sides the player tagged as the same person share an id (the tag only groups; profiles
        # get numbered pseudonyms); untagged sides stay anonymous, one per match
        ids = [None, None]
        for i, lab in enumerate(live_labels(rec)):
            if lab != ".":
                ids[i] = f"label:{rec.path.parent}:{lab.lower()}"
        me = live_side(rec)
        if me is not None:
            ids[me] = "self"
        return tuple(ids)
    opp = opponent_hash(rec)
    if opp in _PLACEHOLDERS:
        opp = None
    me = self_side(rec)
    ids = [None, None]
    # the first folder is the author's own replays ("self" = P01); every other replay folder is a
    # contributor: their recording player is a new player, and their name codes use their own key,
    # so their ids are kept per folder (2026-10-19, first volunteered data)
    own = rec.path.parent.resolve() == _MAIN_DIR
    ids[me] = "self" if own else f"self:{rec.path.parent}"
    ids[1 - me] = (f"opp:{opp}" if own else f"opp:{rec.path.parent}:{opp}") if opp else None
    return tuple(ids)


_MAIN_DIR = None  # the author's own replays (the first folder given)


def main(rep_dirs, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.t7p"):
        old.unlink()
    # group sides by (player hash, character); unknown players stay one profile per replay side
    groups = collections.OrderedDict()
    global _MAIN_DIR
    _MAIN_DIR = Path(rep_dirs[0]).resolve()
    recs = unique_recordings([r for d in rep_dirs for r in load_dir(d)])
    # folders in the order given (own replays, live data, contributors), replays before live in each:
    # keeps every existing player's pseudonym number stable when new folders are added
    order = {Path(d).resolve(): i for i, d in enumerate(rep_dirs)}
    recs.sort(key=lambda r: (min((i for d, i in order.items() if d in r.path.resolve().parents), default=len(order)),
                             r.path.name.startswith("live_"), r.path.name))
    _PLACEHOLDERS.clear()
    _PLACEHOLDERS.update(placeholder_hashes(recs))
    print(f"{len(_PLACEHOLDERS)} placeholder name hash(es) treated as unknown opponents")
    anon = 0
    for rec in recs:
        ids = player_ids(rec)
        for si, side in enumerate(("p1", "p2")):
            cid = rec.chars[si]
            pid = ids[si]
            if pid is None:
                anon += 1
                pid = f"anon{anon}"
            groups.setdefault((pid, cid), []).append((rec, side))
    # pseudonyms: players numbered by first appearance; hashes themselves are not written out
    player_no = {"self": 1}  # the recording player is always P01
    index = []
    for (pid, cid), sides in groups.items():
        if pid not in player_no:
            player_no[pid] = len(player_no) + 1
        name = f"{char_name(cid).replace(' ', '')}-P{player_no[pid]:02d}"
        chunks, frames, tb = [], 0, [0, 0]
        for rec, side in sides:
            chunks += [c for r in rec.rounds for c in extract_side(r.frames, side)]
            for r in rec.rounds:
                g, b = throw_breaks(r.frames, side)
                tb[0] += g
                tb[1] += b
            frames += sum(len(r.frames) for r in rec.rounds)
        if not chunks:
            continue
        write_profile(out_dir / f"{name}.t7p", cid, name, frames, chunks, tb)
        kinds = collections.Counter(c.kind for c in chunks)
        index.append((cid, name, frames))
        print(f"{name:18} {len(sides)} replay side(s): {frames} frames, {dict(kinds)}")
    index.sort(key=lambda x: (x[0], -x[2]))
    with open(out_dir / "index.txt", "w", encoding="ascii", newline="\n") as f:
        for cid, name, frames in index:
            f.write(f"{cid} {name} {frames}\n")
    print(f"{len(index)} profiles, {len({i[0] for i in index})} characters -> {out_dir}")


if __name__ == "__main__":
    # build_profiles.py <recordings dir> [<more dirs, e.g. live data> ...] <profiles out dir>
    main(sys.argv[1:-1], sys.argv[-1])
