"""Tekken 7 stage ids (exe+0x34DF550) and names.

CONFIRMED: read live from the game and labelled by hand. ASSUMED: from the usual T7 stage
order (every confirmed id so far matched it) — verify by logging the id in practice on that stage.
"""
CONFIRMED = {0: "Mishima Dojo", 3: "Arctic Snowfall", 4: "Twilight Conflict (day)", 30: "Duomo di Sirio",
             2: "Jungle Outpost (evening)", 5: "Dragon's Nest", 31: "Arena", 39: "Kinder Gym", 40: "Infinite Azure",
             35: "Brimstone and Fire", 54: "Twilight Conflict (evening)", 56: "Last Day on Earth",
             33: "G Corp Helipad (night)", 36: "Precipice of Fate", 52: "Hammerhead",
             1: "Forgotten Realm", 53: "Jungle Outpost (day)",
             57: "Cave of Enlightenment", 7: "Devil's Pit", 9: "Abandoned Temple",
             59: "Island Paradise", 6: "Souq", 8: "Mishima Building", 32: "G Corp Helipad (day)", 37: "Violet Systems", 41: "Geometric Plane", 55: "Infinite Azure (night)", 51: "Howard Estate", 58: "Vermillion Gates"}  # 59: balcony break (confirmed in play)
# Devil's Pit: walled in early rounds; on a match-point round (round 3 or 4) it becomes wall-less
# (confirmed in play). Wall state there must come from the round's own data, not a fixed map.
ROUND_DEPENDENT_WALLS = {7}
# Forgotten Realm: 3 breakable floors (4 levels) before the bottom level (confirmed in play).
FLOOR_BREAK_LEVELS = {1: 4}
# Variants can have their own id (Twilight Conflict day 4 vs evening 54), so Jungle Outpost (day)
# is NOT known to be 2.
ASSUMED = {}
UNKNOWN_ID = []
INFINITE = {3, 40, 55}

NAME_TO_ID = {v: k for k, v in {**ASSUMED, **CONFIRMED}.items()}

# Variants with the same wall layout (confirmed in play): wall maps are built and shared per group.
LAYOUT_GROUPS = [
    {4, 54},          # Twilight Conflict day / evening
    {2, 53},          # Jungle Outpost evening / day
    {32, 33},         # G Corp Helipad day / night (confirmed in play)
    {35, 36},         # Brimstone and Fire / Precipice of Fate: same layout (confirmed in play)
    {39, 41},         # Kinder Gym / Geometric Plane: same layout (full lap matches within 74 units, 2026-10-18)
]
JUNGLE_OUTPOST_DAY_SAME_LAYOUT_AS = 2


def layout_of(stage_id):
    """Canonical id whose wall map this stage uses."""
    for g in LAYOUT_GROUPS:
        if stage_id in g:
            return min(g)
    return stage_id
