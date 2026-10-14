"""Cut each replay side into action chunks: the exact raw inputs a player used, the situation
they used them in, and what happened.

Chunk kinds
  attack   from a free/grounded state into an attack, through strings and combo follow-ups
  neutral  up to NEUTRAL_WINDOW frames of movement/guarding while free (no attack started)
  wakeup   inputs while knocked down
Situation (decided at the chunk's first input frame, from the actor's point of view)
  sit      neutral | opp_startup | opp_recovery | opp_stunned | opp_juggled | opp_down | self_down
  dist     close | mid | far | vfar
  adv      opponent frames of recovery left (punish window) when sit = opp_recovery, else 0
  oppatk   opponent's attack type when it is attacking (1 low, 2 mid, 5 high, 10 throw, ...)
  life     behind | even | ahead (own health % minus opponent's, +-LIFE_MARGIN)
  rage     own rage active, opp_rage  opponent's rage active
Health: the recorded hp field (+0x1508, percent) when the recording has it; older recordings
rebuild it from damage_taken (percent = damage / 1.8). Rage: the rage aura timer (+0x8E0) was
active within RAGE_WINDOW frames while hp <= RAGE_HP+5 (the timer also runs for another
effect at high health); older recordings estimate it as hp <= RAGE_HP (no "spent" info).
"""
from dataclasses import dataclass, field, asdict

NEUTRAL_WINDOW = 20
# A neutral window ends only where the direction input has settled (unchanged for NEUTRAL_SETTLE
# frames), never inside a tap: fixed 20-frame cuts split ~40% of dashes (f,n,f) in half, and the
# halves were replayed as unrelated chunks (AI dashed 7-8/min vs the players' 18-24).
NEUTRAL_SETTLE = 9
NEUTRAL_MAX = 40
LEAD_MAX = 10
LIFE_MARGIN = 20
RAGE_WINDOW = 16
RAGE_HP = 25          # estimate for recordings without the rage timer; calibrate with calibrate_rage()
MAX_HP = 180.0
MAX_GETUP = 240       # frames; longer floor time is a KO / round end that health rebuilding missed


def annotate_round(frames):
    """Add derived per-frame '_hp' and '_rage' to both players (in place)."""
    real = "rage_fx" in frames[0]["p1"] and any(0 < int(f["p1"]["hp"]) <= 100 for f in frames[:30])
    for side in ("p1", "p2"):
        hp, last_d, last_rage = 100.0, 0, -10**9
        for i, f in enumerate(frames):
            p = f[side]
            if real:
                p["_hp"] = int(p["hp"])
                if int(p["rage_fx"]) != 0:
                    last_rage = i
                # +0x8E0 also runs for some other effect at high health; rage starts at ~21-25%
                p["_rage"] = (i - last_rage) <= RAGE_WINDOW and 0 < p["_hp"] <= RAGE_HP + 5
            else:
                d = int(p["damage_taken"])
                if d != last_d and d > 0:
                    hp = max(0.0, hp - d / MAX_HP * 100)
                last_d = d
                p["_hp"] = round(hp)
                p["_rage"] = 0 < hp <= RAGE_HP
    return real


def life_bucket(me, opp):
    d = me["_hp"] - opp["_hp"]
    return "behind" if d < -LIFE_MARGIN else "ahead" if d > LIFE_MARGIN else "even"

GROUND_SIMPLE = {12, 13, 15, 8, 11, 20}
CROUCH_SIMPLE = {5, 6, 7}
JUGGLE_SIMPLE = {14}


def cls(p):
    if int(p["simple_state"]) in JUGGLE_SIMPLE:
        return "juggled"
    if int(p["stun"]) != 0:
        return "stun"
    if int(p["simple_state"]) in GROUND_SIMPLE:
        return "down"
    if int(p["attack_type"]) != 0 or int(p["startup"]) > 0:
        return "attack"
    return "free"


# Recorded raw_held directions are screen-absolute (0x4 = left, 0x8 = right) while the AI must
# send player-relative bits (0x1 u, 0x2 d, 0x4 back, 0x8 forward). Rebuild them from the game's
# decoded relative direction (input_dir, +0xDDC) and keep the raw button bits.
DIR_TO_REL = {32: 0, 16: 0x4, 64: 0x8, 4: 0x2, 2: 0x6, 8: 0xA, 256: 0x1, 128: 0x5, 512: 0x9}


def rel_held(p):
    return DIR_TO_REL.get(int(p["input_dir"]), 0) | (int(p["raw_held"]) & ~0xF)


def dist_bucket(d):
    d = float(d)
    return "close" if d < 900 else "mid" if d < 1500 else "far" if d < 2300 else "vfar"


def remaining(p):
    return max(0, int(p["recovery"]) - int(p["move_timer"]))


def situation(me, opp):
    oc = cls(opp)
    if cls(me) == "down":
        return "self_down", 0
    if oc == "attack":
        if int(opp["move_timer"]) <= int(opp["startup"]):
            return "opp_startup", 0
        return "opp_recovery", remaining(opp)
    if oc == "stun":
        return "opp_stunned", 0
    if oc == "juggled":
        return "opp_juggled", 0
    if oc == "down":
        return "opp_down", 0
    return "neutral", 0


@dataclass
class Chunk:
    kind: str
    sit: str
    dist: str
    adv: int
    oppatk: int
    inputs: list                       # raw held words, one per frame, starting at the first input
    lead: int = 0                      # frames of input before the first move starts
    moves: list = field(default_factory=list)
    startup: int = 0
    outcome: str = "none"              # whiff | blocked | hit | counter | juggle | interrupted | none
    damage: int = 0
    prev: str = ""                     # first move of the actor's previous attack chunk
    t: int = 0                         # frame index in round (for debugging)
    life: str = "even"                 # behind | even | ahead
    rage: bool = False                 # own rage active
    opp_rage: bool = False             # opponent's rage active
    crouch: bool = False               # the actor was crouching when the chunk's input began
    advb: int = 1                      # frame advantage when it began: 0 minus, 1 even, 2 plus
    first_result: str = "whiff"        # how the first move went: whiff | blocked | hit
    continued: bool = False            # the player went on into another attack after it

    def to_json(self):
        d = asdict(self)
        d["inputs"] = rle(self.inputs)
        return d


def rle(seq):
    out = []
    for v in seq:
        if out and out[-1][0] == v:
            out[-1][1] += 1
        else:
            out.append([v, 1])
    return out


def first_move_result(frames, side, opp_side, t0, move):
    """Outcome of the chunk's first move alone (before any follow-up): whiff, blocked or hit."""
    start_out = int(frames[t0][opp_side]["hit_outcome"])
    i = t0
    while i < len(frames) and int(frames[i][side]["move_id"]) == move:
        o = int(frames[i][opp_side]["hit_outcome"])
        if o != start_out and o != 0:
            return "blocked" if o in (1, 2) else "hit"
        i += 1
    return "whiff"


def lead_in(frames, side, t0, free_from):
    """First frame of the motion that produced the move starting at t0 (the press is at t0-1)."""
    held = [rel_held(frames[i][side]) for i in range(len(frames))]
    j = t0 - 1
    lo = max(free_from, t0 - LEAD_MAX)
    # walk back while the input keeps changing (motion inputs: d, df, f...); stop at a long hold
    while j - 1 >= lo:
        if held[j - 1] == 0:
            break
        if held[j - 1] == held[j] and (j - 2 < lo or held[j - 2] == held[j - 1]):
            break
        j -= 1
    return max(j, lo)


def outcome_of(frames, opp_side, a, b):
    out, dmg = "whiff", 0
    last_d = int(frames[a][opp_side]["damage_taken"])
    for i in range(a, b):
        o = frames[i][opp_side]
        h = int(o["hit_outcome"])
        if h in (1, 2) and out == "whiff":
            out = "blocked"
        elif h in (10, 11):
            out = "counter" if out in ("whiff", "blocked") else out
        elif h >= 12 and out in ("whiff", "blocked"):
            out = "hit"
        elif h in (3, 4, 5, 7):
            out = "juggle"
        d = int(o["damage_taken"])
        if d != last_d and d > 0:
            dmg += d
        last_d = d
    return out, dmg


def extract_side(frames, side):
    if "_hp" not in frames[0]["p1"]:
        annotate_round(frames)
    chunks = _extract_side(frames, side)
    opp_side = "p2" if side == "p1" else "p1"
    adv = advantage_track(frames, side)
    for c in chunks:
        me, op = frames[c.t][side], frames[c.t][opp_side]
        c.life, c.rage, c.opp_rage = life_bucket(me, op), bool(me["_rage"]), bool(op["_rage"])
        c.advb = adv_bucket(adv[c.t])
    return chunks


ADV_RESET = 30  # both free this long = back to plain neutral (even)


def advantage_track(frames, side):
    """Per-frame frame advantage of `side` after the last exchange: when both are free, how many
    frames earlier the actor became free than the opponent (negative = opponent was free first =
    actor was minus). While the opponent is still recovering/stunned: +their remaining frames."""
    opp_side = "p2" if side == "p1" else "p1"
    out = [0] * len(frames)
    free_since = {side: 0, opp_side: 0}
    prev = {side: None, opp_side: None}
    for i, f in enumerate(frames):
        for s_ in (side, opp_side):
            c = cls(f[s_])
            if c == "free" and prev[s_] != "free":
                free_since[s_] = i
            prev[s_] = c
        me_c, op_c = prev[side], prev[opp_side]
        if me_c != "free":
            out[i] = 0
        elif op_c in ("attack",):
            out[i] = remaining(f[opp_side])
        elif op_c in ("stun", "juggled", "down"):
            out[i] = 10
        else:
            a = free_since[opp_side] - free_since[side]
            out[i] = 0 if i - max(free_since[side], free_since[opp_side]) > ADV_RESET else a
    return out


def adv_bucket(a):
    return 0 if a <= -4 else 2 if a >= 4 else 1


def calibrate_rage(recs):
    """Health % at which the rage timer first starts each round (recordings that have it)."""
    out = []
    for rec in recs:
        for r in rec.rounds:
            if not annotate_round(r.frames):
                continue
            for side in ("p1", "p2"):
                for f in r.frames:
                    if int(f[side]["rage_fx"]) != 0 and int(f[side]["hp"]) > 0:
                        out.append(int(f[side]["hp"]))
                        break
    return out


def _extract_side(frames, side):
    opp_side = "p2" if side == "p1" else "p1"
    n = len(frames)
    chunks, prev_attack = [], ""
    i, free_from, next_neutral = 1, 0, 0
    while i < n:
        me, opp = frames[i][side], frames[i][opp_side]
        c, c_prev = cls(me), cls(frames[i - 1][side])
        if c == "attack" and c_prev in ("free", "down"):
            t0 = i
            j = lead_in(frames, side, t0, free_from)
            sit, adv = situation(frames[j][side], frames[j][opp_side])
            # follow strings and combo follow-ups
            k, idle = t0, 0
            moves = []
            interrupted = False
            while k < n:
                mk, ok = frames[k][side], frames[k][opp_side]
                ck = cls(mk)
                if ck in ("stun", "juggled"):
                    interrupted = True
                    break
                if ck == "attack":
                    idle = 0
                    if not moves or moves[-1] != int(mk["move_id"]):
                        moves.append(int(mk["move_id"]))
                else:
                    idle += 1
                    if idle >= 4 and cls(ok) not in ("juggled", "stun"):
                        break
                k += 1
            end = k - idle if not interrupted else k
            out, dmg = outcome_of(frames, opp_side, t0, min(n, k + 1))
            ch = Chunk("attack", sit, dist_bucket(frames[j][side]["distance"]), adv,
                       int(frames[j][opp_side]["attack_type"]),
                       [rel_held(frames[x][side]) for x in range(j, max(end, t0 + 1))],
                       lead=t0 - j, moves=moves, startup=int(frames[t0][side]["startup"]),
                       outcome="interrupted" if interrupted and out == "whiff" else out, damage=dmg,
                       prev=prev_attack, t=j)
            ch.crouch = int(frames[j][side]["simple_state"]) in CROUCH_SIMPLE
            if moves:
                ch.first_result = first_move_result(frames, side, opp_side, t0, moves[0])
                ch.continued = len(moves) >= 2
            chunks.append(ch)
            prev_attack = str(moves[0]) if moves else ""
            i = max(end, i + 1)  # the idle frames after the attack belong to the next neutral window (dashes start there)
            free_from = next_neutral = i
            continue
        if c == "free" and c_prev != "free":
            free_from = next_neutral = i
        if c == "free" and i >= next_neutral:
            # neutral window: at least NEUTRAL_WINDOW frames, then up to the next settled input;
            # stops early if an attack starts
            dirs = lambda x: rel_held(frames[x][side]) & 0xF
            k = i
            while k < n and cls(frames[k][side]) == "free":
                k += 1
                if k - i >= NEUTRAL_MAX or (k - i >= NEUTRAL_WINDOW and
                                            len({dirs(x) for x in range(k - NEUTRAL_SETTLE, k)}) == 1):
                    break
            next_neutral = k
            if k - i >= 6:
                sit, adv = situation(me, opp)
                # movement/guard only: button bits that started nothing (round-intro mashing,
                # buffered presses) would make the AI fire random moves when replayed
                chunks.append(Chunk("neutral", sit, dist_bucket(me["distance"]), adv, int(opp["attack_type"]),
                                    [rel_held(frames[x][side]) & 0xF for x in range(i, k)], prev=prev_attack, t=i))
        # getup chunks: everything the player input from being launched/knocked down until free
        # (air control, tech rolls, getup attacks) — players input on ~58% of juggle frames
        if c in ("down", "juggled") and c_prev not in ("down", "juggled"):
            # split at landing: "air" = launched until first grounded frame (air control, techs),
            # "wakeup" = grounded until free (getup choices). Replaying airborne idle time on the
            # floor made the AI lie still for seconds.
            k = i
            while k < n and cls(frames[k][side]) in ("down", "juggled"):
                k += 1
            land = i
            while land < k and cls(frames[land][side]) != "down":
                land += 1
            # KO'd until the round ends is not getup behaviour (an 820-frame "getup" made the AI
            # lie still): skip segments that reach the round end or where health hits 0
            ko = k >= n or any(int(frames[x][side].get("_hp", 1)) <= 0 for x in range(i, k))
            if ko:
                i = k
                continue
            if land > i:
                chunks.append(Chunk("air", "self_down", dist_bucket(me["distance"]), 0, int(opp["attack_type"]),
                                    [rel_held(frames[x][side]) for x in range(i, land)], prev=prev_attack, t=i))
            if k > land and k - land <= MAX_GETUP:
                chunks.append(Chunk("wakeup", "self_down", dist_bucket(frames[land][side]["distance"]), 0,
                                    int(frames[land][opp_side]["attack_type"]),
                                    [rel_held(frames[x][side]) for x in range(land, k)], prev=prev_attack, t=land))
        i += 1
    return chunks


if __name__ == "__main__":
    import collections
    import sys
    sys.path.insert(0, __import__("os").path.dirname(__file__))
    from load import load_dir, char_name
    for rec in load_dir(sys.argv[1]):
        for si, side in enumerate(("p1", "p2")):
            cid = rec.chars[si]
            allc = [c for r in rec.rounds for c in extract_side(r.frames, side)]
            kinds = collections.Counter(c.kind for c in allc)
            atk = [c for c in allc if c.kind == "attack"]
            print(f"{rec.path.name} {side} {char_name(cid)}: {dict(kinds)}")
            print("   attack sits:", dict(collections.Counter(c.sit for c in atk)))
            print("   outcomes:", dict(collections.Counter(c.outcome for c in atk)))
            top = collections.Counter(tuple(c.moves[:3]) for c in atk).most_common(6)
            print("   top move seqs:", top)
            pun = [c for c in atk if c.sit == "opp_recovery"]
            print("   punish-window starts:", [(c.adv, c.moves[:2], c.startup, c.outcome) for c in pun[:8]])


THROW_CODES = (0x1C, 0x1E, 0x1F)  # defender's throw_tech low byte at the grab (0x1D = idle)


def throw_breaks(frames, side):
    """(grabs, breaks) for `side` as the defender: a grab = the defender's throw_tech code changes to a
    break code while the opponent's move is a throw (level 10); broken = the thrower goes into a shared
    system move (< 1000, not idle) within 40 frames instead of finishing the throw."""
    att = "p2" if side == "p1" else "p1"
    n = len(frames)
    grabs = breaks = 0
    i = 1
    while i < n:
        a, d, pd = frames[i][att], frames[i][side], frames[i - 1][side]
        code = int(d.get("throw_tech", 0)) & 0xFF
        if int(a["attack_type"]) == 10 and code in THROW_CODES and (int(pd.get("throw_tech", 0)) & 0xFF) != code:
            grabs += 1
            gm = int(a["move_id"])
            for x in range(i, min(n, i + 40)):
                m = int(frames[x][att]["move_id"])
                if m != gm:
                    breaks += m < 1000 and m != 32769
                    break
            i += 40
            continue
        i += 1
    return grabs, breaks
