# Changelog - T7 Ghost AI

Version numbers: the **number** changes when new features are added (v1.0a -> v1.1a), the **letter**
changes when the Ghosts get new training data (v1.1a -> v1.1b).

## v1.1a (2026-10-25)
- **Special fights, one checkbox per kind** (F1 > Ghost Battle Mode Settings): *Boss fights*, *Turbo
  Battle*, *Heist Battle*, *Double Damage Battle* and *Aerial Juggle Battle* - instead of one switch for all
  of them. All off by default as before; an older t7ghost.ini with special fights on keeps every kind on.
  Special fights now also come after the end-of-match menu: any ticked kind after *Next opponent* - a boss
  or Heist Battle can surprise you before the opponent you picked, and *Rematch* / *Rematch on a new stage*
  then lead to that opponent; Turbo, Double Damage and Aerial Juggle after *Rematch on a new stage*; a plain
  *Rematch* always stays the same fight.
- **Recorded punishes for every Ghost** - 36 characters (was 14): Akuma, Alisa, Bryan, Eliza, Ganryu,
  Gigas, Heihachi, Hwoarang, Jack-7, King, Kuma, Lars, Lei, Lili, Lucky Chloe, Negan, Noctis, Paul,
  Shaheen, Steve, Xiaoyu and Yoshimitsu are new. Where a character has no 15 f punish, or a hard one,
  slower or faster alternatives are recorded instead (e.g. 13 f and 18 f low punishes), and short-range
  punishers fall back to a longer-range one when they won't reach.
- **Heavy punishes are always punished**: a move that is -22 or worse on block is always punished
  perfectly once Punishment Proficiency is 20% or more (the Medium Challenge preset and up).
- **Combo routes for every Ghost** - all 36 characters (was 13), 475 recorded routes: Akuma, Alisa, Bryan,
  Devil Jin, Dragunov, Eliza, Feng, Ganryu, Gigas, Heihachi, Jack-7, Kazumi, Kazuya, Kuma, Lars, Lei, Lili,
  Lucky Chloe, Negan, Noctis, Shaheen, Steve and Xiaoyu are new, and more launchers lead into recorded
  combos for the others (e.g. Josie's ws2,1, Julia's ws1, Negan's ws2, Hwoarang's crouching ub+3). King's
  combos end with his grab. Kazuya, Steve and Noctis also combo after their crumple stuns.
- Combo routes start reliably after launchers that hand over into a follow-up move (e.g. Josie's df+2 used
  to skip her combo most of the time).
- **Taunts** for Heihachi and Lucky Chloe (19 characters).
- **Preset costumes in Ghost Battle**: a new *Costumes* choice, *1P-4P and Preset Costumes* - every
  opponent wears a random one of the character's 1P-4P or Preset costumes. (Presets used to leave the
  loading screen hanging; Treasure Battle never dressed its opponents in them.) *Allow Customized
  Costumes* stays the default (now called *Default Treasure Battle CPU Costumes*).
- **Your own customizations on Ghosts**: Ghosts can wear the costumes you saved in Customization. New
  *Costumes* choices: *1P-4P, Preset, and Player Customizations*, *1P-4P and Player Customizations*,
  *Player's Own Customizations Only* and *Random* (Treasure Battle's CPU outfits, 1P-4P, Preset costumes
  and your customizations). The new *Customization slot to force on Ghosts* picks the slot: *Any* (a random
  one of the slots you saved for that character) or 1-10; characters without a customization in the
  forced slot wear a 1P-4P costume.
- **Health modifier** (F1 > Game Modifiers): a *Health* slider, 25-500% of the normal health bar for both
  players (200% = twice the damage to KO). Offline battles only, from the next match; 100% (off) by default.
- **No Power Crush modifier** (F1 > Game Modifiers): power crush moves lose their armor (hits interrupt them,
  with normal damage) and their smoke effect. Offline battles only, from the next match; off by default.
- **No Screw modifier** (F1 > Game Modifiers): screw moves give a normal juggle hit instead of spinning the
  opponent to extend the combo. Offline battles only, from the next match; off by default.
- **Startup warnings**: if the mod can't start, a Windows message box now says why instead of the mod
  silently doing nothing - an unsupported TEKKEN 7 version (the mod needs the latest Steam version), or
  a settings panel blocked by another overlay or graphics tool.
- README.txt has a *Troubleshooting* section (no note in the top-left corner, F1 does nothing).
- **The mod file is now `dinput8.dll`** (was `version.dll`): on some Windows installations Windows
  loads its own `version.dll` before the game can pick up the mod's, so the mod never started (no log,
  no F1 panel). **Updating: delete the old `version.dll`** - while it is still there the new
  `dinput8.dll` stays switched off and a message box says so. The mod still works renamed to
  `version.dll` (e.g. when another mod needs the name `dinput8.dll`).
- **Data Harvesters v0.6**: also `dinput8.dll` now (installing one replaces the mod, as before); same
  recording as v0.5. Their README says where to send your data: DreamOfCasinoSoul@proton.me.
- F1 panel: the special fight checkboxes moved to the end of Ghost Battle Mode Settings, and a new
  **PLEASE HELP!** category explains how you can help the Ghosts improve by sending play data.

## v1.0a
- **Ghost AI Presets.** A "Ghost AI Preset" dropdown in the F1 panel: *Fair*, *Medium Challenge*,
  *Challenging* (default) and *Unfair* - or *Custom Settings* for your own. While a preset is chosen the
  Ghost AI settings are locked to it; your own values are kept for Custom Settings.
  - *Fair*: the Ghosts play almost purely like the recorded players (human reaction time, no input
    reading, the players' own guarding, punishing, throw breaks and combos). A Ghost may still play
    easier than the real player when there isn't enough recorded data to capture their playstyle.
  - *Unfair*: everything turned up - very fast reactions, reads every move, perfect guards and
    punishes.
- **Game Modifiers** (new F1 category; offline battles only, each takes effect from the next match,
  all off by default): **No Rage** - nobody gets rage, no Rage Arts or Rage Drives.
- **Taunts.** After KO'ing you, a Ghost sometimes taunts you (more often when it won with most of its
  health left). Taunts recorded for 17 characters.
- **Ghost Battle costumes**: the "Force default costumes" checkbox is now a *Costumes* dropdown -
  *Allow Customized Costumes*, *1P Costumes Only*, *1P, 2P, 3P, 4P Costumes Only* (3P/4P for the
  characters that have them).
- **Strings with a delayed follow-up** (e.g. Julia's df+3, 1+2) no longer catch the Ghost after it
  blocks the first hit: it keeps guarding while the next hit is on its way.
- Recorded combos and punishes always play to their end (a hit-confirm check could cut them short).
- The F1 panel is tidier: *Ghost AI Settings*, *Ghost Battle Mode Settings*, *Game Modifiers* and
  *Imitation Settings* are dropdowns that open when you need them.
- t7ghost.ini now points to the GitHub page and tekkenmods.com for updates.

## v0.8.5
- **Punishment Proficiency** (replaces the Punish checkbox): the chance that a punish is perfect - the
  best punish for that opening (-10, -12 to -15 and slower on block, lows too) on the first possible
  frame. Recorded punishes for 14 characters, each knowing how far its punishers reach and falling back
  to the next best one when it won't.
- Better punishing in general: moves that slide or land into a follow-up are seen as still recovering,
  and strings are punished after their last hit.

## v0.8
- **Combos** became a three-way setting: *Player's own combo habits*, *Finish combos when appropriate*
  (full combo routes, ended early when a hit misses or a wall is close) and *Always finish combos*.
  Combo routes recorded for 13 characters (usual launchers, counter-hit launchers, low parries).

## v0.7.2
- Every Ghost in one public build: 63 recorded players, 36 characters; more Ghost training data added
  (Noctis new).
- Native end-of-match menu in the game's own result window: Rematch, Rematch on a new stage, Next
  opponent, Return to Character Select, Return to Main Menu.
- The main menu calls the mode GHOST BATTLE; treasure chests can be hidden on the match HUD.

## v0.6.3
- The Ghost AI in Arcade Battle and Treasure Battle.
- **Ghost Battle Mode** (Treasure Battle reworked): round count from your Game Options, a random stage
  every match, opponents from the game's ranked ghosts with a Ghost profile, special fights / default
  costumes / random player options, end-of-match menu.

## v0.6
- **Wall awareness**: wall maps for every stage; Ghosts sidestep away from walls, defend corners and
  press you into walls.
- Training on live (online) match data; merged profiles by default; configurable F1 menu key.

## v0.5.1
- First public tech demo: replay-trained Practice AI with the F1 settings panel; Data Harvesters for
  replays and live matches.
