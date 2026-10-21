# Changelog - T7 Ghost AI

Version numbers: the **number** changes when new features are added (v1.0a -> v1.1a), the **letter**
changes when the Ghosts get new training data (v1.0a -> v1.0b).

## v1.0b (work in progress)
A progress build: v1.0b is not finished yet (more changes to the Ghosts' behaviour are coming). Everything
below stays part of v1.0b.
- **Recorded punishes for every Ghost** - 36 characters (was 14): Akuma, Alisa, Bryan, Eliza, Ganryu,
  Gigas, Heihachi, Hwoarang, Jack-7, King, Kuma, Lars, Lei, Lili, Lucky Chloe, Negan, Noctis, Paul,
  Shaheen, Steve, Xiaoyu and Yoshimitsu are new. Where a character has no 15 f punish, or a hard one,
  slower or faster alternatives are recorded instead (e.g. 13 f and 18 f low punishes), and short-range
  punishers fall back to a longer-range one when they won't reach.
- **Heavy punishes are always punished**: a move that is -22 or worse on block is always punished
  perfectly once Punishment Proficiency is 20% or more (the Medium Challenge preset and up).
- **Combo routes for 21 characters** (was 13): Devil Jin, Feng, Ganryu, Gigas, Heihachi, Kazumi, Kazuya
  and Negan are new; Josie's ws2,1, Julia's ws1 and Negan's ws2 now lead into recorded combos. Kazuya also
  has combos after his crumple stuns.
- Combo routes start reliably after launchers that hand over into a follow-up move (e.g. Josie's df+2 used
  to skip her combo most of the time).
- **Taunts** for Heihachi and Lucky Chloe (19 characters).
- **Startup warnings**: if the mod can't start, a Windows message box now says why instead of the mod
  silently doing nothing - an unsupported TEKKEN 7 version (the mod needs the latest Steam version), or
  a settings panel blocked by another overlay or graphics tool.
- README.txt has a *Troubleshooting* section (no note in the top-left corner, F1 does nothing).

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
