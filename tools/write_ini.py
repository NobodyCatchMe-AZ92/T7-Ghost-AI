"""Write t7ghost.ini in the release layout: version header, then each
option followed by its explanation, two blank lines between options.

  py -I tools/write_ini.py OUT.ini [--from EXISTING.ini]

--from keeps the values (and [profiles] lines) of an existing ini, old or new layout; without it the
defaults below are written (release template).
"""
import configparser
import sys

VERSION = "0.6.3"  # keep in step with T7_VERSION_STR in src/main.cpp
DOWNLOAD = ". . . "  # pending: the download link for updates

# (key, default, explanation)
AI = [
    ("mod_enabled", "1", '1 = the replay-trained AI replaces the CPU offline: Practice "CPU" dummy at Ultra Hard, Arcade and Treasure Battle (never online)'),
    ("arcade", "1", "1 = also in Arcade Battle (characters without a profile keep the game's CPU)"),
    ("treasure_battle", "1", "1 = also in Treasure Battle (characters without a profile keep the game's CPU)"),
    ("reaction_frames", "14", "frames before the AI reacts to what you do (60 = 1 second). 13-15 = strong human"),
    ("input_read_chance", "0", '0-100: chance per move that the AI "reads" your move instantly (cheat). 0 = fair'),
    ("punish", "1", "1 = always punish when a punisher the player used fits the window"),
    ("combos", "1", "1 = continue juggles with the player's own combos"),
    ("guard_skill", "100", "0-100: how often perceived attacks are guarded correctly (scaled by the player's guard habit)"),
    ("decision_skill", "60", "0-100: favour the player's attacks that landed (hit/counter/launch) over whiffs and stuffed moves"),
    ("adapt", "70", "0-100: how strongly the AI adapts to YOUR habits learned during play (kept per character you use)"),
    ("throw_break_chance", "0", "throw breaks: 0 = each player's own break rate from the replays (most ~5-15%), 1 = never, 2-100 = % chance (100 = always)"),
    ("wall_awareness", "1", "1 = walls change how it plays: sidesteps away from walls, defends corners (and throws out of them),\n"
                            "; presses you harder with heavy moves when your back is to a wall. 0 = off (if it plays strangely)"),
]
PROFILES_NOTE = ("; per character id: c21=merged (default: all recorded players as one),\n"
                 "; c21=hybrid (best player's style, other players fill gaps + share punishes/combos), or c21=Katarina-P01 (that player only)")
UI = [
    ("menu_key", "F1", "key that opens/closes the in-game settings panel: F1-F12, Insert, Home, End, PageUp, a letter...;\n"
                       "; modifiers allowed, e.g. Ctrl+F1, Shift+Home"),
]
GB = [
    ("enabled", "1", "1 = Ghost Battle Mode: Treasure Battle uses the round count from your Game Options"),
    ("random_stage", "1", "1 = a random stage every fight (the game keeps one stage for 5 fights)"),
    ("ghost_opponents", "1", "1 = opponents only from characters with a Ghost profile"),
    ("special_fights", "1", "1 = keep Treasure Battle's special fights (Turbo etc.), 0 = every fight is a regular fight"),
    ("default_costumes", "0", "1 = opponents wear their default costume, 0 = the ghosts' own customised outfits"),
    ("end_menu", "1", "1 = after each match: rematch, rematch on a new stage, 3 next opponents, character select, main menu"),
    ("random_player", "0", "1 = each ghost imitates one random recorded player, 0 = the usual profile ([profiles], merged by default)"),
]
SCAN_NOTE = "; dev tool region: data = exe .data section, state = newest fight-state block"


def main():
    out = sys.argv[1]
    vals, profiles, region = {k: d for k, d, _ in AI}, [], "data"
    ui_vals = {k: d for k, d, _ in UI}
    gb_vals = {k: d for k, d, _ in GB}
    if "--from" in sys.argv:
        cp = configparser.ConfigParser(inline_comment_prefixes=(";",), strict=False)
        cp.optionxform = str
        cp.read(sys.argv[sys.argv.index("--from") + 1], encoding="utf-8")
        if cp.has_section("ai"):
            ai = cp["ai"]
            if "enabled" in ai and "mod_enabled" not in ai:  # pre-0.5 name
                vals["mod_enabled"] = ai["enabled"]
            for k in vals:
                if k in ai:
                    vals[k] = ai[k]
        if cp.has_section("ui"):
            for k, _, _ in UI:
                if k in cp["ui"]:
                    ui_vals[k] = cp["ui"][k]
        if cp.has_section("ghost_battle"):
            for k, _, _ in GB:
                if k in cp["ghost_battle"]:
                    gb_vals[k] = cp["ghost_battle"][k]
        if cp.has_section("profiles"):
            profiles = [f"{k}={v}" for k, v in cp["profiles"].items()]
        if cp.has_section("scan"):
            region = cp["scan"].get("region", region)
    lines = [f";T7 GHOST AI MOD - VERSION {VERSION}", f";FIND THE LATEST RELEASE AT {DOWNLOAD}", "", "[ai]"]
    for k, _, note in AI:
        lines += [f"{k}={vals[k]}", f"; {note}", "", ""]
    lines += ["", "[ui]"]
    for k, _, note in UI:
        lines += [f"{k}={ui_vals[k]}", f"; {note}", "", ""]
    lines += ["", "[ghost_battle]"]
    for k, _, note in GB:
        lines += [f"{k}={gb_vals[k]}", f"; {note}", "", ""]
    lines += ["", "[profiles]", PROFILES_NOTE] + profiles + ["", "", "", "[scan]", SCAN_NOTE, f"region={region}", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
