# T7-Ghost-AI
This is a mod project to improve the AI opponents in Tekken 7. An AI profile is built for each character in the game based on recorded data from the matches of real players. This makes the AI behave like real players in movements, combo routes, favorite moves, and overall strategy.

## INTRODUCTION
This is a mod project to improve the AI opponents in Tekken 7. This will extend the playability of the game into the far future since we all know Tekken 7's vanilla AI is arguably the weakest in the whole series and very boring to fight.

The mod works similarly to the Ghosts in Tekken 8: an AI profile is built for each character in the game based on recorded data from the matches of real players. This makes the AI behave like real players in movements, combo routes, favorite moves, and overall strategy.

You can adjust various parameters for the Ghost AI in realtime during a match: for example, set the percentage chance it has of breaking throws, or the chance it has to cheat and read your actual inputs rather than observe you visually.

Video demonstration of Katarina's Ghost AI (work in progress): https://streamable.com/447xkg
     

## HELP THE MOD IMPROVE - COLLECT GHOST DATA AND SEND IT TO ME!
I need help collecting DATA for the AI character profiles. The data I have been able to collect by myself has been extremely limited - only 60 matches - so a lot of characters and play styles are totally missed by the mod.

Your data is totally anonymized - unique players are identified only with a special number value, never any identifying username or Steam ID. The default setting of the mod aggregates all collected data for a character - no matter the player - into one big character profile.

There are two ways you can help me collect data:


#### Data Collection Method 1: Replay Harvester
Install the Replay Harvester DLL into your game and watch your saved replays. You can press *ctrl+F8* to make the harvester automatically navigate the replay menu and go to the next replay (meaning you can walk away from the game and grab a coffee while you let some replays playback). Harvested data gets spat out into a folder next to your game exe called "t7GhostReplayData" which you then send to me

### Data Collection Method 2: Live Harvester
Install the LIVE Harvester DLL into your game and play/spectate online matches like you normally would. Harvested data gets spat out into a folder next to your game exe called "t7GhostLIVEplayData" which you then send to me

I did investigate a method of having people instead send me their t7 replay save files and converting them to work on my account, but that is a very risky effort and can cause corruption of my game install. Trying to read the raw data from those save files themselves would be fruitless since the AI needs to understand not just the stored player inputs from those files but also the in-engine reactions etc which can only be observed through in-game playback.


## DOWNLOADING AND INSTALLING
Available for download are:

A public tech demo of the Ghost AI you can try for yourself. Only Katarina and Ganryu AIs are included in this tech demo. You can tune their AI in realtime by pressing F1 to bring up the AI menu. Install by dropping the files in: TEKKEN 7\TekkenGame\Binaries\Win64 (next to the game exe). Then open practice mode, set the mode to "VS CPU", and set difficulty to Ultra Hard against Katarina or Ganryu.
The Data Harvesters: if you want to help populate the character data for this mod, you install version.dll next to your game .exe and then send me the Ghost Data folders that get generated (t7GhostLIVEplayData and t7GhostReplayData - NEVER send the folder "t7GhostHarvest_private"). Use the Replay Harvester to harvest character data while you watch your saved replays, use the Live Harvester to harvest data while you play online VS matches. 


### Please send all volunteered Ghost Data to the email address: DreamOfCasinoSoul@proton.me 

OR upload to mediafire and send the link in this mod page's comments.
NEVER upload the folder "t7GhostHarvest_private". This contains data that COULD reveal your Steam username. The other folders are completely anonymized; the data you send to me doesn't reveal anything about you, your PC, or your Steam account.



PLEASE HELP MAKE THIS MOD BETTER! IT DEPENDS ON BEING FED LOTS OF DATA - WHICH I CAN'T DO ALONE! We can improve the Tekken 7 offline experience into something truly fun, starting with this mod!

Thank you!




## How it works (short)

1. **Recording.** A `version.dll` proxy loaded by the game reads the game's per-frame fight-state
   snapshots while replays play back, and writes them as JSONL (moves, frame data, stun, positions,
   health, inputs). It only reads memory.
2. **Profiles.** The Python pipeline in `analysis/` cuts each player's play into decisions ("chunks":
   what they did in which situation, at which distance, with which frame advantage) and learns move
   frame data from the replays.
3. **Playing.** In Practice, with the dummy set to CPU on Ultra Hard, the mod drives the opponent from
   the profile: it picks the recorded player's own choices for the situation, punishes what it blocked,
   continues juggles with the player's combos, and learns your habits as you play.

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for the memory layout, file formats and pipeline.

## Repository layout

| Path | What |
|---|---|
| `src/main.cpp` | the mod: loader thread, input hook, recorder, settings, hotkeys |
| `src/brain.hpp`, `src/adapt.hpp` | the AI: profile playback, punishing, guarding, live adaptation |
| `src/overlay.hpp` | in-game settings panel (Dear ImGui on the game's D3D11 swap chain) |
| `src/harvest.cpp` | the shareable read-only harvesters (replay / live) |
| `src/t7.hpp`, `src/names.hpp` | Tekken 7 memory layout; player-name pseudonymisation |
| `analysis/` | Python: load recordings, build profiles and frame data |
| `tools/` | ini writer, replay auto-pilot, memory peek/sampler helpers |
| `dist/t7ghost.ini` | default settings shipped with releases |
| `third_party/` | MinHook (BSD-2), Dear ImGui (MIT) |

## Building

Windows, Visual Studio 2022 (x64 tools). From the repo root:

```
build.bat           -> build\version.dll (dev build) and build\public\version.dll (release build)
build_harvest.bat   -> build\harvest_replay\version.dll and build\harvest_live\version.dll
```

Profiles (Python 3.12, standard library only):

```
py analysis/build_profiles.py <recordings dir> <profiles out dir>
py analysis/framedata.py      <recordings dir> <framedata.txt>
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
