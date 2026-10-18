# T7 Ghost AI — technical overview

Target: Tekken 7, Steam, final build (`TekkenGame-Win64-Shipping.exe`, PE timestamp `0x63EB1C19`,
2023-02-14, Unreal Engine 4). The mod checks the timestamp and stays inert on any other build.

## Loading

`version.dll` in `TekkenGame/Binaries/Win64` is a proxy: it forwards all 15 exports of the system
`version.dll` (one generic 8-argument x64 forwarder, `src/proxy_version.cpp`) and starts one worker
thread from `DllMain`. Hooks use MinHook.

## Memory layout (`src/t7.hpp`)

**Fight-state ring.** `[[exe + 0x35037B8] + 0x8]` is the base of 8 game-state copies (stride `0x1E710`),
one per recent frame; the frame counter is at `+0xA70`, player 2's block at `+0x7840`. Reading the
newest copy every 2 ms gives every frame, also during replay fast-forward (with gaps). Startup fields
(`+0x7780`, `+0x7784`) exist only in these copies.

**Live player objects.** P1 `exe + 0x34EA7D0`, P2 `exe + 0x34EDE40` (same field offsets).

**Per-player fields** (selection; full list in `t7.hpp`):

| Field | Offset | Notes |
|---|---|---|
| char | `0xDC` | character id |
| move_timer | `0x1F0` | frames into the current move |
| attack_type | `0x329` (u8) | hit level; 0 = not an attack, 1 low, 2-4 mid, 5 high, 10 throw |
| move_id | `0x350` | |
| recovery | `0x39C` | total frames of the current move |
| hit_outcome | `0x3D8` | block / hit / counter-hit codes |
| simple_state / stun | `0x428` / `0x42C` | standing, crouching, juggled, down...; stun timer |
| throw_tech | `0x450` | low byte at a grab: `0x1C` break 1, `0x1E` break 1+2, `0x1F` break 2 (`0x1D` idle) |
| rage_fx | `0x8E0` | rage aura timer |
| input_dir | `0xDDC` | player-relative direction |
| raw_held | `0xE4C` | raw button word (screen-absolute directions) |
| hp | `0x1508` | health percent |
| distance | `0x1450` | |
| px / py / pz | `0x1610` / `0x1614` / `0x1618` | continuous root position |
| startup / startup_end | `0x7780` / `0x7784` | ring copies only; one frame behind on a new move |

**Globals:** game mode `exe + 0x34D67A8` (0 menus, 1 online, 2 Practice and offline VS, 3 replay), Practice dummy
is CPU `exe + 0x3447820`, Practice CPU level `exe + 0x3447834` (4 = Ultra Hard), stage id
`exe + 0x34DF550` (ids in `analysis/stages.py`).

**Input.** `decode_input` at `exe + 0x747F50`, signature `(Player*, int side, u32 held, u32 pressed)`,
called per player per frame. The AI overrides `held`/`pressed` for P2. Bits (player-relative):
up `0x1`, down `0x2`, back `0x4`, forward `0x8`; buttons 1 `0x4000`, 2 `0x8000`, 3 `0x1000`, 4 `0x2000`.

## Recordings (JSONL)

One line per frame: `{"f": frame, "p1": {...fields...}, "p2": {...}}`, plus
`{"reset": 1, ...}` when the frame counter restarts (new round) and probe rows:
`{"names_probe": [[base, offset, enc, len, hash12], ...], "side_info": [...], "stage": id, ...}`.
`names_probe` holds only the first 12 hex digits of HMAC-SHA256(key, name bytes); the key is 32
random bytes created on the recording PC (`private/salt.bin`) and never shared, so names can't be
recovered and the same player keeps one code per key.

## Profiles (`.t7p`, text, version `T7P 4`)

```
char <id> / name <Character-Pnn> / frames <n> / tb <grabs> <breaks>
k <move> <cont% whiff> <cont% blocked> <cont% hit> <n> <n> <n>      hit-confirm per string starter
c <kind> <sit> <dist> <adv> <oppatk> <startup> <lead> <outcome> <damage> <first_move>
  <life> <rage> <opp_rage> <crouch> <advb> <inputs: value:count,...>                 one decision
```

Kinds: attack, neutral (movement/guard), wakeup, air. Situations: neutral, opponent startup /
recovery / stunned / juggled / down, self down. `advb` = frame-advantage bucket at the decision.

`framedata.txt`: `m <char> <move> <startup> <level> <damage> <block_adv> <hit_adv> <launch%> <n_block>
<n_hit> <range> <start_range>` — learned from the recordings. `learned_framedata.txt`:
`l <char> <move> <frames>` — frames the AI really had after blocking a move, measured in play.
`opponent_<char>.txt` (`OPP 1`): the AI's model of the human per character (attack rates and levels
per situation, getup habits, seen moves, punish results, lost interrupts, sidestep results).

## The AI (`src/brain.hpp`, `src/adapt.hpp`)

Per frame, with a perception delay (`reaction_frames`; or instantly for moves "read" by the
`input_read_chance` cheat): guard strings, buffered punishes on the last blockstun frame (window from
learned readings, then frame data, then live recovery), whiff punishes (dashing in if needed),
juggles with the player's combos, hit-confirm, throw breaks, then the recorded player's own choice
for the situation (frame advantage, distance, life lead, rage), weighted by how it worked out in the
replays. `adapt.hpp` models the human live and biases those choices.

## Settings panel (`src/overlay.hpp`)

Hooks `IDXGISwapChain::Present` / `ResizeBuffers` (found through a throwaway D3D11 swap chain),
draws Dear ImGui over the game's back buffer while open, and subclasses the window procedure for
input. Changes go to the running AI and to `t7ghost.ini` (WritePrivateProfileString).

## Which fights the AI plays

Offline only (game mode 2). Battle type `[exe+0x34D67B0]`: 0 Arcade, 1 Treasure, 2 VS, 3 Practice.
Practice: the dummy when it is set to CPU at Ultra Hard. Arcade / Treasure (ini `arcade`,
`treasure_battle`): whichever side the game controls (player object `+0x1570` = 1 for the CPU / dummy,
0 for a human). A new opponent character starts a fresh AI state.

## Ghost Battle Mode (`src/ghostbattle.hpp`, `src/gbmenu.hpp`)

Treasure Battle sets up every next fight in `exe+0x439340`. It fills the battle settings object
(vcall `+0x10` on `[exe+0x34CE030]+0x460`, then `+0x10`): `+0x0C/+0x10` character per side, `+0x14`
stage, `+0xA0` rounds to win (always 2 in Treasure), `+0xB8 + side*0x460` costume (mode 0 = default,
4 = the ranked ghost's own). The fight type picks the opponent: 0/4/5/6 = a ranked ghost from the
online-style list `[exe+0x34CEA10]` (8 rank tiers, 0x40-byte records: `+8` character, `+0x10` name),
applied by `exe+0x439C90`; 5 is Turbo; other types are special fights. The mod hooks both functions:
it hands `0x439C90` a ghost whose character has a profile (character first, uniformly, then any rank),
and after `0x439340` sets the rounds from Game Options (`[[exe+0x34CE450]+0xA1E0]`, what VS reads), a
random stage, and optionally default costumes. Special fights off: the fight type is set to 0 before
the call. The ghost list is rebuilt between fights, so a chosen ghost is remembered by character and
name and looked up again at setup.

End-of-match menu: the game's own two-option menu (Next match / Start Over, Return to Main Menu) is up
while `[exe+0x34D6C04]` is non-zero; `[exe+0x379B16C]` is 3 after the KO and while paused, 2 in a fight.
The mod draws its menu over it and hooks `XInputGetState` (xinput1_3, imported by the game): neutral
pads while the menu is open, then scripted presses on the native menu (A = continue, Down + A = main
menu; it opens on the top entry and wraps). Character Select: continue, press Start until the next fight
is paused (not possible during the intros), then Up, Up (= Character Select), A, Left (= Yes), A.

## Harvesters (`src/harvest.cpp`)

The recorder alone, with no hooks: replay variant (records while game mode = 3, optional
auto-navigation through the Battle Log with Ctrl+F8) and live variant (any non-replay fight; writes
frames 6 behind the newest because online fights roll back).

## Live (online) recordings

The live harvester records any non-replay fight. Online fights roll back, and the recorder misses single
frames now and then; the loader fills gaps of up to 4 frames by repeating the previous frame (timing
stays exact), drops segments with bigger holes, and keeps only segments where someone lost health.
Practice sessions in the same mode id are dropped (no KO, health refills). Which side the recording
player was, and optional same-person tags for other players, come from a hand-written
`my_matches.txt` next to the files (`<file> ... <P1|P2|-|?> <L tag> <R tag>`); untagged fighters stay
anonymous, one per match.

## Wall maps (`walls.txt`, `analysis/build_walls.py`, `src/walls.hpp`)

Built from Practice laps along every wall (root positions logged by the dev build). Per stage and
level (floor height) and per break phase, the outline is the convex hull of dense position cells, in
root coordinates (distance 0 = pinned against the wall).

```
infinite <stage>                                  no walls (Arctic Snowfall, Infinite Azure)
alias <stage> <stage>                             same layout (day/night variants, twin stages)
level <stage> <name> <floor height> <breakable floor>
phase <stage> <level> <phase> <n> x z x z ...     outline per break state, counter-clockwise
edge  <stage> <level> <phase> <index> <balcony|breakable>
```

At runtime each fighter's level is the highest floor at or below them; a level's phase is the first
one that contains every position seen beyond its current outline this round (so a break is noticed the
moment someone is past the old wall); a single-level stage where someone is far outside every outline
counts as wall-less for the round (Devil's Pit at match point). New round = both fighters back at full
health.

**Wall awareness** (ini `wall_awareness`): the distance behind each fighter (a ray from them, away
from the other) and whether two differently-facing walls are close (a corner) give the state: no walls
/ calm / back to the wall (< 2.5 m) / cornered / opponent at the wall (< 3 m). Back to the wall: more
guarding and movement, sidesteps toward the side with more room (a 1-frame up/down tap after neutral;
which way "up" goes on each screen side is learned from the first clear sidestep). Cornered: guards
more and tries a throw within 1.2 m. Opponent at the wall: more attacks, weighted toward heavy hitters
(damage, launchers) that splat.

## Stage ids

0 Mishima Dojo, 1 Forgotten Realm, 2/53 Jungle Outpost, 3 Arctic Snowfall, 4/54 Twilight Conflict,
5 Dragon's Nest, 6 Souq, 7 Devil's Pit, 8 Mishima Building, 9 Abandoned Temple, 30 Duomo di Sirio,
31 Arena, 32/33 G Corp Helipad, 35 Brimstone and Fire, 36 Precipice of Fate, 37 Violet Systems,
39 Kinder Gym, 40/55 Infinite Azure, 41 Geometric Plane, 51 Howard Estate, 52 Hammerhead,
56 Last Day on Earth, 57 Cave of Enlightenment, 58 Vermillion Gates, 59 Island Paradise.
