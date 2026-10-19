"""Write t7ghost.ini in the release layout (the author's sample): version header, then each
option followed by its explanation, two blank lines between options.

  py -I tools/write_ini.py OUT.ini [--from EXISTING.ini]

--from keeps the values (and [profiles] lines) of an existing ini, old or new layout; without it the
defaults below are written (release template).
"""
import configparser
import sys

VERSION = "1.0a"  # keep in step with T7_VERSION_STR in src/main.cpp (letter = new ghost data, number = new features)
DOWNLOAD = "https://github.com/NobodyCatchMe-AZ92/T7-Ghost-AI"

# (key, default, explanation)
AI = [
    ("mod_enabled", "1", '1 = the replay-trained AI replaces the CPU offline: Practice "CPU" dummy at Ultra Hard, Arcade and Treasure Battle (never online)'),
    ("arcade", "1", "1 = also in Arcade Battle (characters without a profile keep the game's CPU)"),
    ("treasure_battle", "1", "1 = also in Treasure Battle (characters without a profile keep the game's CPU)"),
    ("ai_preset", "3", "Ghost AI Preset: 0 = Custom Settings (the values below), 1 = Fair, 2 = Medium Challenge, 3 = Challenging, 4 = Unfair. A preset overrides the values below (they are kept for Custom Settings)."),
    ("reaction_frames", "14", "frames before the AI reacts to what you do (60 = 1 second). 13-15 = strong human"),
    ("input_read_chance", "0", '0-100: chance per move that the AI "reads" your move instantly (cheat). 0 = fair'),
    ("punish_proficiency", "50", "0-100: Punishment Proficiency. 0 = the player's own punishers and timing; 1-100 = chance that a punish "
                                "is perfect (the best punish for that opening, on the first possible frame)"),
    ("combo_style", "1", "0 = the player's own combo habits, 1 = finish combos when appropriate (recorded routes, "
                         "ended early when a hit misses or a wall is close), 2 = always finish combos"),
    ("guard_skill", "100", "0-100: how often perceived attacks are guarded correctly (scaled by the player's guard habit)"),
    ("decision_skill", "60", "0-100: How much the Ghosts favour attacks that are statistically proven to land more often than ones that whiffed or got counter-hit."),
    ("adapt", "70", "0-100: how strongly the AI adapts to YOUR habits learned during play (kept per character you use)"),
    ("throw_break_chance", "0", "throw breaks: 0 = each player's own break rate from the replays (most ~5-15%), 1 = never, 2-100 = % chance (100 = always)"),
    ("wall_awareness", "1", "1 = on, 0 = off. Tick to let the location of nearby walls influence the Ghost's behaviors. Ghosts will sidestep away from walls, defend near corners, pressure you against walls."),
]
PROFILES_NOTE = ("; per character id: c21=merged (default: all recorded players as one),\n"
                 "; c21=hybrid (best player's style, other players fill gaps + share punishes/combos), or c21=Katarina-P01 (that player only)")
UI = [
    ("menu_key", "F1", "key that opens/closes the in-game settings panel: F1-F12, Insert, Home, End, PageUp, a letter...;\n"
                       "; modifiers allowed, e.g. Ctrl+F1, Shift+Home"),
]
MODS = [
    ("no_rage", "0", "1 = on, 0 = off. No Rage: nobody gets rage (no rage damage boost, Rage Arts or Rage Drives). Offline battles only, from the next match."),
]
GB = [
    ("enabled", "1", "1 = on, 0 = off. Converts Treasure Battle into Ghost Battle - round counts affected by your Game Options, Random Stages every new match, and more."),
    ("random_stage", "1", "1 = on, 0 = off. A new random stage every match (the game normally keeps one stage for five fights)."),
    ("ghost_opponents", "1", "1 = on, 0 = off. Only queue characters that have Ghost data. Characters without Ghost data will never appear with this option ticked."),
    ("special_fights", "0", "1 = on, 0 = off. Tick to allow the special fight encounters from the vanilla Treasure Battle mode (e.g. Turbo Mode and Boss Fights). Untick to disable these fights completely."),
    ("costumes", "0", "0 = Allow Customized Costumes (the ranked ghosts' own outfits), 1 = 1P Costumes Only, 2 = 1P, 2P, 3P, 4P Costumes Only (a random one per opponent; 3P and 4P only for the characters that have them)."),
    ("end_menu", "1", "1 = on, 0 = off. Tick to enable Ghost Battle's custom end of match menu options (choose the next opponent, rematch with same stage, rematch on new stage, etc)"),
    ("native_menu", "1", "1 = on, 0 = off. Show the end-of-match menu inside the game's own result window. 0 = the overlay menu instead."),
    ("rename", "1", "1 = on, 0 = off. The main menu calls the mode GHOST BATTLE instead of TREASURE BATTLE."),
    ("hide_chests", "1", "1 = on, 0 = off. No treasure chest icons on the match HUD (the results screen still shows your rewards)."),
    ("random_player", "0", "1 = on, 0 = off. Tick to make it so every opponent you encounter is only based on ONE ghost profile. Ticking this option makes the opponents you encounter less intelligent overall, but more unique in playstyle (e.g. you may encounter two Paul opponents who play very differently). Recommended is unticked/disabled."),
]
SCAN_NOTE = "; dev tool region: data = exe .data section, state = newest fight-state block"


def main():
    out = sys.argv[1]
    vals, profiles, region = {k: d for k, d, _ in AI}, [], "data"
    ui_vals = {k: d for k, d, _ in UI}
    gb_vals = {k: d for k, d, _ in GB}
    mod_vals = {k: d for k, d, _ in MODS}
    if "--from" in sys.argv:
        cp = configparser.ConfigParser(inline_comment_prefixes=(";",), strict=False)
        cp.optionxform = str
        cp.read(sys.argv[sys.argv.index("--from") + 1], encoding="utf-8-sig")  # tolerate a BOM
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
            gbs = cp["ghost_battle"]
            if "default_costumes" in gbs and "costumes" not in gbs:  # pre-0.9: on = 1P costumes only
                gb_vals["costumes"] = "1" if gbs["default_costumes"].strip() == "1" else "0"
        if cp.has_section("modifiers"):
            for k, _, _ in MODS:
                if k in cp["modifiers"]:
                    mod_vals[k] = cp["modifiers"][k]
        if cp.has_section("profiles"):
            profiles = [f"{k}={v}" for k, v in cp["profiles"].items()]
        if cp.has_section("scan"):
            region = cp["scan"].get("region", region)
    lines = [f";T7 GHOST AI MOD - VERSION {VERSION}",
             f";CHECK FOR UPDATES: the GitHub page {DOWNLOAD} or tekkenmods.com",
             "", "[ai]"]
    for k, _, note in AI:
        lines += [f"{k}={vals[k]}", f"; {note}", "", ""]
    lines += ["", "[ui]"]
    for k, _, note in UI:
        lines += [f"{k}={ui_vals[k]}", f"; {note}", "", ""]
    lines += ["", "[ghost_battle]"]
    for k, _, note in GB:
        lines += [f"{k}={gb_vals[k]}", f"; {note}", "", ""]
    lines += ["", "[modifiers]"]
    for k, _, note in MODS:
        lines += [f"{k}={mod_vals[k]}", f"; {note}", "", ""]
    lines += ["", "[profiles]", PROFILES_NOTE] + profiles + ["", "", "", "[scan]", SCAN_NOTE, f"region={region}", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
