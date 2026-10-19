# T7-Ghost-AI
A mod project to improve the AI opponents in Tekken 7. An AI profile is built for each character from
recorded data of real players' matches, so the AI behaves like real players: their movement, combo
routes, favourite moves and overall strategy.

## INTRODUCTION
This mod is here to extend the playability of Tekken 7 far into the future - we all know Tekken 7's
vanilla AI is arguably the weakest in the whole series and very boring to fight.

The mod works similarly to the Ghosts in Tekken 8: an AI profile is built for each character based on
recorded data from the matches of real players. This makes the AI behave like real players in
movements, combo routes, favourite moves, and overall strategy - with sharp execution on top: it
punishes what it blocks, punishes your whiffs, confirms its hits and breaks throws.

It also **learns you**. While you play, the Ghost notices your habits - how often you attack, how you
get up, which of your moves it can punish or sidestep - and remembers them for next time, separately
for every character you play.

You can adjust the Ghost AI in real time during a match: press **F1** for the settings menu and, for
example, set its reaction speed, its chance of breaking throws, or the chance it has to cheat and read
your actual inputs rather than react to what it sees.

Video demonstration of Katarina's Ghost AI (work in progress): https://streamable.com/447xkg

### What's new in v0.8.5
- **Punishment Proficiency** (F1 slider, replaces the Punish checkbox). 0% = the Ghost punishes with the
  real player's own punishers and timing. 1-100% = the chance that a punish is *perfect*: the best punish
  for that opening (-10, -12, -13, -14, -15 and slower on block, lows too), on the first possible frame,
  never delayed or too slow. Recorded for 14 characters (Jin, Kazuya, Asuka, Devil Jin, Feng, Dragunov,
  Leo, Claudio, Katarina, Josie, Kazumi, Lee, Miguel, Julia); each one knows how far its punishers reach
  and falls back to the next best when one won't reach.
- Better punishing in general: moves that slide or land into a follow-up animation are recognised as
  still recovering, and strings are punished after their last hit, not between hits.
- Launch punishes go straight into the recorded combo routes.

### What's new in v0.8
- **Combos** is now a three-way setting: *Player's own combo habits*, *Finish combos when appropriate*
  (default: full combo routes, ended early when a hit misses or a wall is close) and *Always finish
  combos*. Combo routes are recorded for 13 characters (Paul, King, Yoshimitsu, Hwoarang, Jin, Asuka,
  Leo, Claudio, Katarina, Josie, Lee, Miguel, Julia) for their usual launchers, counter-hit launchers and
  low parries; other launchers use the player's own combos.

### What's new in v0.7.2
- **All the Ghosts in one public build**: more Ghost training data added - 63 recorded players over
  **36 characters**, with **Noctis** as a new character.
- **Native end-of-match menu.** Ghost Battle's menu now lives in the game's own result window, in the
  game's own style: *Rematch*, *Rematch on a new stage*, *Next opponent* (choose with left/right),
  *Return to Character Select* and *Return to Main Menu*. Character Select takes you straight there.
  The previous overlay menu is still available (untick *Native look* in F1).
- The main menu calls the mode **GHOST BATTLE**.
- **Hide treasure chests**: no chest icons on the match HUD (your rewards still show after the match).
- New defaults: special fights off, chests hidden; options renamed and described more clearly.

### What's new in v0.6.3
- **Ghost AI in Arcade Battle and Treasure Battle**, not just Practice - on either side of the screen.
  Characters without a Ghost profile keep the game's own CPU. Both can be switched off in the F1 menu.
- **Ghost Battle Mode** - Treasure Battle reworked into a Ghost gauntlet:
  - the round count follows your Game Options (Treasure Battle normally forces first to 2);
  - a random stage every fight (instead of the same stage for five fights);
  - opponents are only characters with a Ghost profile - taken from the game's own ranked ghosts (any
    rank), so they keep their names and **custom outfits**; or tick *Force default costumes*;
  - *Special fights* (Turbo and the like) can be switched off;
  - *Random player per ghost*: each Ghost imitates one recorded player instead of the merged profile.
- **End-of-match menu** in Ghost Battle Mode, driven by your controller: *Rematch*, *Rematch on a new
  stage*, a choice of **three next opponents**, *Return to Character Select* and *Return to Main Menu*.
- Treasure Battle's own rewards and progress are left untouched.

### What's new in v0.6
- **Wall awareness.** Every stage in the game is mapped - all floors, balconies, breakable walls and
  breakable floors. With its back to a wall the Ghost sidesteps away (guarding in between); backed into
  a corner it defends harder and looks for a throw to escape; and when *your* back is to the wall it
  presses you with heavy, wall-splatting moves. You can switch it off in the F1 menu.
- **Live wall indicator** in the F1 menu showing what the Ghost currently thinks about the walls.
- **Online matches as training data.** The new Live Harvester records matches you play or spectate.
- **More data:** the profiles now come from about 110 matches (saved replays plus online matches),
  and all players of a character are merged into one big profile by default.


## HELP THE MOD IMPROVE - COLLECT GHOST DATA AND SEND IT TO ME!
I need help collecting DATA for the AI character profiles. What I could collect by myself is still very
limited - around 110 matches - so a lot of characters and play styles are missing from the mod.

Your data is anonymized - players are identified only by a code, never by a username or Steam ID. By
default the mod merges all collected data for a character, no matter the player, into one big
character profile.

There are two ways you can help me collect data:

#### Data Collection Method 1: Replay Harvester
Install the Replay Harvester DLL into your game and watch your saved replays. Press *Ctrl+F8* on the
Battle Log with your first replay highlighted, and the harvester navigates the replay menu by itself
and plays one replay after another (so you can walk away and grab a coffee while it runs; press
*Ctrl+F8* again to stop it). Turn off the in-replay tips ("Recommended Combo" / "Recommended
Punishment") first - they pause the replays. Harvested data goes into a folder next to your game exe
called "t7GhostReplayData", which you then send to me.

#### Data Collection Method 2: Live Harvester
Install the LIVE Harvester DLL into your game and play or spectate online matches like you normally
would. Harvested data goes into a folder next to your game exe called "t7GhostLIVEplayData", which you
then send to me. Only human players help the Ghosts: online matches, spectating, and offline matches
between two people are great - please **don't** send offline fights against the CPU.

I did look into having people send me their Tekken 7 replay save files instead, but loading another
account's replay saves corrupts the save data, and reading those files directly wouldn't help anyway:
the AI needs not just the stored inputs but how the game engine reacted to them, which can only be
observed by playing the replay back in-game.


## DOWNLOADING AND INSTALLING
Available for download (see Releases):

**The public build** of the Ghost AI with every Ghost (36 characters). Drop the contents of its `Win64`
folder into `TEKKEN 7\TekkenGame\Binaries\Win64` (next to the game exe), then play Ghost Battle (the
reworked Treasure Battle), Arcade Battle, or Practice with the opponent set to "CPU" on "Ultra Hard".
Press F1 to tune the Ghosts in real time. The package's README.txt explains everything.

**The Data Harvesters**, if you want to help populate the character data: put the harvester's
`version.dll` next to your game exe, then send me the Ghost Data folders it creates
(t7GhostLIVEplayData and t7GhostReplayData). Use the Replay Harvester while you watch your saved
replays, and the Live Harvester while you play or spectate online matches.

### Please send all volunteered Ghost Data to the email address: DreamOfCasinoSoul@proton.me

OR upload to mediafire and send the link in this mod page's comments.

**NEVER send the folder "t7GhostHarvest_private".** It holds the secret key that turns player names
into the anonymous codes in your data - with that key, someone could check guessed names against the
codes. The data folders themselves contain only gameplay (moves, positions, health, inputs) with
players as codes: nothing about you, your PC or your Steam account.

PLEASE HELP MAKE THIS MOD BETTER! IT DEPENDS ON BEING FED LOTS OF DATA - WHICH I CAN'T DO ALONE! We can
improve the Tekken 7 offline experience into something truly fun, starting with this mod!

Thank you!


## How it works (short)

1. **Recording.** A `version.dll` proxy loaded by the game reads the game's per-frame fight state while
   replays play back (or while you play online, with the Live Harvester) and writes it to text files:
   moves, frame data, stun, positions, health, inputs. It only reads memory.
2. **Profiles.** The Python pipeline in `analysis/` cuts each player's play into decisions - what they
   did in which situation, at which distance and frame advantage, with how much life and rage - and
   learns every move's frame data (speed, damage, reach, block and hit advantage) from the recordings.
3. **Playing.** In Practice (opponent set to CPU on Ultra Hard), Arcade Battle and Treasure / Ghost
   Battle, the mod takes over the CPU's controller. Each frame it sees the fight through a short reaction delay and picks the
   recorded players' own choice for the situation - and on top: buffered punishes from learned frame
   data, whiff punishes (dashing in when needed), juggles with the players' own combos, hit-confirms,
   throw breaks, and what it has learned about you.
4. **Combo routes and punishes.** Hand-made demonstrations recorded in Practice (Ctrl+F12 in the dev
   build): `analysis/combos.py` and `analysis/punishes.py` group repeated attempts into routes and
   punishes (kept when done 3+ times), which the Ghost replays frame by frame after a launch, or on the
   first free frame after blocking a punishable move (Combos / Punishment Proficiency settings).
5. **Walls.** Every stage was mapped by walking along all of its walls in Practice. The Ghost knows at
   every moment which floor each fighter is on, which walls have been broken this round, and how far
   each fighter's back is from a wall - and plays accordingly.

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for the memory layout, file formats, wall maps and pipeline.

## Repository layout

| Path | What |
|---|---|
| `src/main.cpp` | the mod: loader thread, input hook, recorder, settings, hotkeys |
| `src/brain.hpp`, `src/adapt.hpp` | the AI: profile playback, punishing, guarding, live adaptation, wall behaviour |
| `src/walls.hpp` | wall maps: floors, wall breaks and live wall distances |
| `src/overlay.hpp` | in-game settings panel (Dear ImGui on the game's D3D11 swap chain) |
| `src/ghostbattle.hpp`, `src/gbmenu.hpp` | Ghost Battle Mode (Treasure Battle hooks) and its end-of-match menu |
| `src/gbtext.hpp` | the GHOST BATTLE name on the main menu |
| `src/menutrace.hpp` | dev-build recon tools for the game's Scaleform menus |
| `src/harvest.cpp` | the shareable read-only harvesters (replay / live) |
| `src/t7.hpp`, `src/names.hpp` | Tekken 7 memory layout; player-name pseudonymisation |
| `analysis/` | Python: load recordings (replay and live), build profiles, frame data and wall maps; combo routes and punishes from Practice demonstrations |
| `tools/` | ini writer, wall-survey checker, replay auto-pilot, memory peek/sampler helpers |
| `dist/t7ghost.ini`, `dist/walls.txt` | default settings and the stage wall maps shipped with releases |
| `dist/combos/` | the recorded combo routes (`routes_<char>.txt`) and punishes (`punishes_<char>.txt`) |
| `third_party/` | MinHook (BSD-2), Dear ImGui (MIT) |

## Building

Windows, Visual Studio 2022 (x64 tools). From the repo root:

```
build.bat           -> build\version.dll (dev build) and build\public\version.dll (release build)
build_harvest.bat   -> build\harvest_replay\version.dll and build\harvest_live\version.dll
```

Profiles and maps (Python 3.12, standard library only):

```
py analysis/build_profiles.py <recordings dir> [<more dirs, e.g. live data>] <profiles out dir>
py analysis/framedata.py      <recordings dir> [<more dirs>] <framedata.txt>
py analysis/build_walls.py    <walls_survey.txt> <walls.txt>
py analysis/combos.py         <combo recordings dir>     (routes_<char>.txt)
py analysis/punishes.py       <combo recordings dir>     (punishes_<char>.txt)
```

## Privacy

No player names are ever stored. Name-like strings are hashed in-process with a random key that never
leaves the recording PC; profiles use pseudonyms (P01, P02, ...). Recordings, profiles and other
training data are not part of this repository.

## Credits

By t7ghostauthor. Made with AI assistance (Anthropic's Claude).
Uses [MinHook](https://github.com/TsudaKageyu/minhook) and [Dear ImGui](https://github.com/ocornut/imgui)
(licenses in `third_party/`). Memory-layout starting points from the TekkenBot project's notes.
Not affiliated with or endorsed by Bandai Namco. Tekken is a trademark of Bandai Namco Entertainment Inc.
