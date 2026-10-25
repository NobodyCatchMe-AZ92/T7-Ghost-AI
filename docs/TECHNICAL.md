# T7 Ghost AI — technical overview

Target: Tekken 7, Steam, final build (`TekkenGame-Win64-Shipping.exe`, PE timestamp `0x63EB1C19`,
2023-02-14, Unreal Engine 4). The mod checks the timestamp and stays inert on any other build (with a
one-time message box saying so; another one appears when the overlay can't hook `Present` or start).

## Loading

`dinput8.dll` in `TekkenGame/Binaries/Win64` is a proxy (the game imports DINPUT8; no system DLL depends
on it, unlike `version.dll`, which some Windows installations load from System32 first). It forwards
the exports of the system DLL of its own file name - DirectInput8Create and the COM exports, or the 15
exports of `version.dll` when renamed (one generic 8-argument x64 forwarder, `src/proxy_version.cpp`,
`src/version.def`) - and starts one worker thread from `DllMain`. `src/loader.hpp` keeps one copy per
process (`Local\t7ghost_<pid>`) and stays off with a message box when an old `version.dll` of the mod is
next to it. Hooks use MinHook.

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
random stage, and the Costumes option. Treasure fight types: 1/2 boss fights (Devil Kazumi and the
special opponents), 4 Double Damage Battle, 5 Turbo Battle, 6 Aerial Juggle Battle (only juggles do
damage), 7 Heist Battle; a kind switched off in the F1 panel is set to 0 before the call. After the
end-of-match menu: a plain Rematch is always a regular fight, Rematch on a new stage allows only 4/5/6
(they keep the chosen ghost), Next opponent allows every ticked kind (a boss or Heist fight first, then
the rematch options lead to the picked opponent). The ghost list is rebuilt between fights, so a chosen ghost is remembered by character and
name and looked up again at setup.

End-of-match menu: the game's own two-option menu (Next match / Start Over, Return to Main Menu) is up
while `[exe+0x34D6C04]` is non-zero; `[exe+0x379B16C]` is 3 after the KO and while paused, 2 in a fight.
The mod hooks `XInputGetState` (xinput1_3, imported by the game): neutral pads while its menu is open,
then the native menu is confirmed for the player (A = continue; for the second option the dialog's index
`+0xB4/+0xE4` is set to 1 first).

The menus are Scaleform movies driven from C++. The result menu is the generic dialog
`SW_UI_Dialog_Steam` (class: build `0x49D600`, cursor `0x49F010`, Decide `0x49F120`; handle methods
`0x2A15xx-0x2A1Bxx`, e.g. selected index `0x2A1890`). Its layout comes from
`_root.SetButtonType("Button_L" | "Button_S", n)` (queued Invoke `0x450F90`), labels from the text
variables `$button_big_text1..n` (`0x458020`), the cursor from "Select"/"Normal" on
`...Buttons_L_mc.Button_L<i>` (`0x454190`). Native look: inside the game's window build the mod changes
that SetButtonType count to its 5 rows (the window box is sized when its "Start" animation first plays),
then sets its labels; its cursor drives the highlight, and the game's "Decide" is played on the chosen
row. Character Select: the native "End Treasure Battle" is confirmed and the next scene request
(`0x20FFB80([exe+0x34D5B58], code)`, `[mgr+0x80]`) is changed to 4 = Character Select, so Treasure
first ends its own way (an earlier direct request left a black screen).

Main menu name: the Scaleform text table entry `menu_main_22` ("TREASURE BATTLE" / "Treasure Battle")
is renamed in memory once the menus are up, heap only, re-checked right before each write.

HUD treasure chests: the clips `_root.HUD_Positioning_mc.TreasureBox_Set_mc...` are played by `0x53CB90`
(show), `0x53CD40` (update) and `0x53C9A0` (end); with "Hide treasure chests" they return without
playing (the results screen is another movie).

## Combo routes and punishes (`analysis/combos.py`, `analysis/punishes.py`, `src/brain.hpp`)

Demonstrations are recorded in Practice with the dev build (Ctrl+F12, `t7ghost/combos/combo_*.jsonl`, the
same per-frame format as other recordings). Repeated attempts are grouped and kept when done 3+ times
(`analysis/combo_keep.txt` / `analysis/punish_keep.txt` add, drop or tune single ones).

Combo routes: a launch is the opponent becoming juggled (simple state 14); the launcher is our move at the
last damage before it, a low parry is attack -> airborne without damage. A route is identified by the move
active on each damage frame. `routes_<char>.txt`, one route per line:
`starter launcher hits damage frames lead | id@frame ... | held:frames,... | frame:carry,... | notation`
(`held` = player-relative input words, `carry` = how far the opponent has travelled, in wall-map units).
At runtime a route starts when our move is a route's launcher within 20 frames of the launch, or when we
left the launcher for its follow-up move at most 8 frames ago (e.g. a df+2 that hands over before the
opponent is airborne); it is timed from the launcher's hit (`route_frame_hit`; its move timer can continue from the move
before). Every Ghost has routes (475 for 36 characters); King's end with his grab (`combos.py --max-gap`
keeps a slow grab ender in the same attempt). Kazuya, Steve and Noctis also have stun routes (starter `stun`, `combos.py --stun`): they start when our hit puts the
opponent into a crumple (simple state 12 / 13 / 15 while stunned). A hit is a change of the opponent's
last-hit damage value; `combos.py --hp-hits` also counts a health-bar drop with an unchanged value (hits
of equal damage in a row, e.g. Heihachi's screw into Raijin 2, 1). Combo style 1 ends it when an
expected hit does not land within 10 frames, when the opponent is free again, or before the hit that would
carry them into a wall (opponent's back distance - 1500 < remaining carry).

Punishes: an opening is the end of our blockstun (a block that began as one stays one while the hit
outcome resets in their follow-up move) or a whiff over our crouch. Window = their remaining recovery on
our last blockstun frame; a move that directly follows one of their attacks counts as its recovery, and
cancel window bit `0x1000000` (+0x788) marks the frame they can act. `punishes_<char>.txt`:
`kind needed_window punisher hits damage frames lead press reach open launches | moves | inputs | damage frames | notation`
(needed window = frames from the first free frame to the hit, startup plus any motion; `reach` = farthest
start distance, negative = a hard limit; `open` = the input frame of the author's first free frame).
Punishment Proficiency rolls once per blockstun; on success the punish needing the most frames that fits
the window and reaches is played (launch punishes first among equals), timed so the author's first free
frame lands on ours. Openings of 22 frames or more are always punished perfectly from Punishment
Proficiency 20%.

## Taunts (`analysis/taunts.py`, `src/brain.hpp`)

Every KO in the demonstration recordings (the loser's health goes to 0, the winner's is above 0) gives one
taunt: the winner's player-relative inputs from the KO frame to the end of the round (trailing idle frames
trimmed). `taunts_<char>.txt`: `char <id>`, then `frames | held:frames,...` per taunt. When a Ghost KOs its
opponent it plays a random taunt of its character with a 15% chance, 75% if its own health is still at
least 75%. A hidden ini key (`[ai] taunts=0`, not written by default) switches it off.

## Ghost AI presets (`src/brain.hpp` kAiPresets)

A preset replaces the Ghost AI values in memory only (reaction time, input read, guard / decision skill,
adaptation, throw breaks, punishment proficiency, combo style, wall awareness); the ini keeps the custom
values. `[ai] ai_preset`: 0 Custom, 1 Fair, 2 Medium Challenge, 3 Challenging (default), 4 Unfair.

## Ghost Battle costumes

The costume of each side is a slot in the battle settings (`+0xB8 + side*0x460`, `+4`, setter
`0x292CD0`) and in the player info (`+0xC0A8`, `0x3CD690`): 0-3 = 1P/2P/3P/4P, 4 = customized (the
ranked ghost's own), 7 and up = Preset 1.. . Which characters have 3P/4P comes from the game's list file
`customize_chara_list` (`[exe+0x34CE858]`, 24-byte entries: u8 character, u8 flag); the presets per
character from `customize_preset_table_cs_s3` (`[exe+0x34CE898]`, 0x198-byte entries with up to 50
preset names). List files: object +0x28 = vector of loaded files, data = `[[file]+0x48]`, data +8 = u16
count, data +0 points to the list's own name.

Presets (and the player's own customizations, slot 6) on the opponent used to leave Ghost Battle's
loading screen hanging: the per-side costume routine `0x40B5D0` dressed only our side. The mod hooks it
to dress the opponent after our side once the busy flag `[exe+0x34D5C10]+0x48` clears, and hooks
`0x28DA90` (behind the Blueprint native GetDesiredCostumeType) to report type 0 for that side.

**The player's own customizations.** `customize1/2.sav` are decrypted at boot (the save system's
`0x3D8A60` copies each loaded file to its registered target) and stay in the customize manager
`[exe+0x34CE668]`. `0x2F9300(mgr, -, char, slot 0-9)` returns a slot's record: `mgr + 0x1D08C +
0x2F8E80(char)*0xFB954 + slot*0x280 + 0x2C` (character indices from 0x27 in the second half at
`+0x2670C5C`); the word at block `+2` is the mask of saved slots (unsaved slots hold the default outfit).
Records keep item ids; the outfit (the costume block's `+0x1B4` layout, 0x1AC bytes) keeps 11 rows of
0x1C from `+4`: item hash `0x4AD5E0(id)`, the row's dword `+8`, 0x14 bytes of colours from `+0xC`
(record rows are 0x20), and at `+0x138` `0x292B50()` of the record dwords `+0x160..+0x260` (without
`+0x1E0`) - the conversion of the Practice Quick Select path `0x40BE90`, verified byte for byte against
the game's own entries. The opponent then gets slot 6 with that outfit.

Costumes option: 0 = the ranked ghosts' own outfits, 1 = 1P, 2 = 1P-4P, 3 = 1P-4P + presets, 4 = 1P-4P +
presets + own customizations, 5 = 1P-4P + own, 6 = own only (1P-4P without one), 7 = Random (also the
ranked ghost's own outfit). Mixed options pick a kind first, then one of it; a forced customization slot
the character lacks falls back to a 1P-4P costume.

## Game Modifiers (`src/main.cpp` mods_tick)

Offline battles only (game mode 2), settings latched at each new match (mode start, Ghost Battle's next
fight, or the fight resuming after a loading screen with both players at full health). **No Rage**: the
player byte `+0x757` bit `0x4` marks rage as used for the round; it is set for both players, so the game
never grants rage (`+0x781` = in rage, `+0xC00` = the aura, synced by `0x722C60`). Clearing those two
instead made the game start rage again every frame.

**Health.** `0x742C70` writes the health percent (`+0x1508`) every frame as current * 100 / max. The
health object is at player `+0x14E0`: max health at `+0x14FC` (16.16 fixed point, 175.0; `+0x14F8` and
`+0x1500` hold the same), current health encoded - check dword `+0x14E8`, key qword `+0x14F0`,
value = `0x255A60(check, key)` (0 when the check fails), new check = `0x2D0EA0(value, key)`; many game
integers use this scheme. The mod scales the three max copies whenever the game sets them (every round
start) and sets the current health to the new max when it was full or goes up (round start, Practice
refill).

**No Power Crush.** The power crush move property (id `0x81BD`, value `0x01640000` = on, 100% damage
taken) sets `+0x6C0` = the armor flag (byte) and `+0x6C4` = the damage percent taken while armored
(`0x7506BA`: damage * it / 100, minimum 1) in the property switch of `0x717750` (cases at `0x71D0F4` /
`0x71D14C`). Their two `mov [rcx+0x6C0], al` are replaced by NOPs while the modifier is on (game mode 2
only; restored otherwise, on the game thread), and a hook on `0x717750` skips property `0x8046` value
`0x47` - the power crush smoke effect.

**No Screw.** `+0xB08` (dword) is -1 from a screw hit until the victim gets up (0 again at the wake-up
reaction); a second screw in the same combo gives a normal juggle reaction. The mod keeps it at -1.

## Harvesters (`src/harvest.cpp`)

The recorder alone, with no hooks (also named `dinput8.dll`): replay variant (records while game mode = 3, optional
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
