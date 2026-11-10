T7 GHOST AI - v1.2a PUBLIC BUILD  (by t7ghostauthor)
=====================================================

Tekken 7's offline CPU replaced by "Ghosts": AI opponents that learned from real players' matches. They
copy how those players actually played - movement, habits, strings, punishes and combos - with sharp
execution, they know the walls of every stage, and they adapt to YOUR habits while you play.

This build ships Ghosts for 40 characters (77 recorded players):
  Akuma, Alisa, Armor King, Asuka, Bryan, Claudio, Devil Jin, Dragunov, Eddy, Eliza, Feng, Ganryu,
  Gigas, Heihachi, Hwoarang, Jack-7, Jin, Josie, Julia, Katarina, Kazumi, Kazuya, King, Kuma, Lars,
  Lee, Lei, Leo, Lili, Lucky Chloe, Marduk, Miguel, Negan, Noctis, Paul, Shaheen, Steve, Xiaoyu,
  Yoshimitsu, Zafina.
Characters without a Ghost play as the game's normal CPU.

NEW IN v1.2a
  - EDDY AND ZAFINA GET GHOSTS (40 characters, 77 recorded players), with recorded punishes and
    combo routes (normal hit, counter hit, low parry) - 563 combo routes in all. More Ghost data for
    Kazumi, Negan, Shaheen, Gigas, Noctis, Devil Jin, Katarina, Asuka, Miguel, Heihachi and others;
    2533 moves of frame data.
  - CHOOSE A SPECIFIC OPPONENT AND STAGE: new option in Ghost Battle's end-of-match menu. It opens
    the game's own Simple Select screen: pick the Ghost to fight next (only characters with a Ghost
    are listed) and its costume, your own character and costume, and the stage.
  - GAME MODIFIERS TAKE EFFECT IMMEDIATELY, even mid-match (Health from the next round).
  - NO WALL BOUND MODIFIER: wall bound moves give a normal wall splat instead (no wall bound combos).
  - BACKDASH DISTANCE and SIDESTEP DISTANCE MODIFIERS: 1-200% of how far backdashes and sidesteps /
    sidewalks travel, for both players.
  - CAMERA DISTANCE (online friendly): 1-200% distance of the normal battle camera; cinematic shots
    and zoom-ins keep their framing. The camera rises a little as it zooms out, or set the height
    yourself with "Adjust camera height independently" + Camera Height Offset.
  - LIFEBAR HUD X / Y / SIZE (online friendly): move the lifebar HUD and make it smaller or bigger.
  - Camera and lifebar HUD options only change your own screen, so they work online too. Every other
    modifier is offline only.
  - Fixed: the main menu sometimes still said TREASURE BATTLE instead of GHOST BATTLE.

NEW IN v1.1b
  - MARDUK AND ARMOR KING GET GHOSTS (38 characters), with recorded punishes and combo routes
    (normal hit, counter hit, low parry) like every other Ghost - 510 combo routes in all.
  - MORE GHOST DATA: King (2.6 -> 38 min), Lee (6 -> 22 min), Kazuya, Negan, Alisa and Akuma gain
    recorded players. 2334 moves of frame data.
  - Armor King's uf+4 launch punish is only used from close enough to connect; farther away he
    punishes with f+2,1.
  - F1 > PLEASE HELP!: up-to-date list of the 13 characters that still have no Ghost.

NEW IN v1.1a
  - RECORDED PUNISHES FOR EVERY GHOST: 36 characters (was 14). Where a character has no 15 f punish,
    or a hard one, faster or slower alternatives are used (e.g. 13 f and 18 f low punishes), and
    short-range punishers fall back to a longer-range one when they won't reach.
  - Moves that are -22 or worse on block are always punished perfectly once Punishment Proficiency
    is 20% or more (Medium Challenge preset and up).
  - COMBO ROUTES FOR EVERY GHOST: all 36 characters (was 13), 475 recorded routes; more launchers
    lead into recorded combos (e.g. Josie's ws2,1, Julia's ws1, Negan's ws2); King's combos end with
    his grab; Kazuya, Steve and Noctis also combo after their crumple stuns. Combos start reliably
    after launchers that hand over into a follow-up move.
  - TAUNTS for Heihachi and Lucky Chloe (19 characters).
  - PRESET COSTUMES IN GHOST BATTLE: new Costumes choice "1P-4P and Preset Costumes" (F1 > Ghost
    Battle Mode Settings) - each opponent wears a random 1P-4P or Preset costume of their character.
  - YOUR OWN CUSTOMIZATIONS ON GHOSTS: more Costumes choices - "1P-4P, Preset, and Player
    Customizations", "1P-4P and Player Customizations", "Player's Own Customizations Only" and
    "Random" - and "Customization slot to force on Ghosts" (Any or 1-10). Ghosts wear the costumes
    you saved in Customization; characters without one in the forced slot wear a 1P-4P costume.
    ("Allow Customized Costumes" is now called "Default Treasure Battle CPU Costumes".)
  - HEALTH MODIFIER: F1 > Game Modifiers > Health - both players' health bars at 25-500% of normal
    (200% = twice the damage to KO). Offline battles only, from the next match; 100% = off.
  - NO POWER CRUSH MODIFIER: F1 > Game Modifiers > No Power Crush - power crush moves lose their armor
    and their smoke effect. Offline battles only, from the next match.
  - NO SCREW MODIFIER: F1 > Game Modifiers > No Screw - screw moves give a normal juggle hit instead of
    extending the combo. Offline battles only, from the next match.
  - If the mod can't start, a message box now says why (unsupported game version, or the settings
    panel blocked by another overlay). See TROUBLESHOOTING below.
  - THE MOD FILE IS NOW dinput8.dll (was version.dll). Updating: delete the old version.dll - see
    "Updating from an older version" below.
  - PLEASE HELP! (new F1 category): how you can help the Ghosts improve by sending play data.

NEW IN v1.0a
  - GHOST AI PRESETS: choose "Fair", "Medium Challenge", "Challenging" (default) or "Unfair" in the
    F1 panel (Ghost AI Settings > Ghost AI Preset) and play - no sliders needed. "Custom Settings"
    unlocks the sliders for your own configuration (kept when you switch back and forth).
      Fair               the Ghosts play almost purely like the recorded players. A Ghost may still play
                         easier than the real player when there isn't enough recorded data of them.
      Medium Challenge   halfway between Fair and Challenging
      Challenging        fast reactions, frequent input reading, perfect guards, strong punishing
      Unfair             everything turned up - ridiculously difficult
  - GAME MODIFIERS (new F1 category; offline battles only, from the next match, all off by default):
      No Rage            nobody gets rage - no rage damage boost, no Rage Arts or Rage Drives
  - TAUNTS: after KO'ing you, a Ghost sometimes taunts you - more often when it won with most of its
    health left.
  - GHOST BATTLE COSTUMES: "Allow Customized Costumes", "1P Costumes Only" or "1P, 2P, 3P, 4P
    Costumes Only" (3P/4P for the characters that have them).
  - Strings with a delayed follow-up (e.g. Julia's df+3, 1+2) no longer catch the Ghost after it
    blocks the first hit.
  - Recorded combos and punishes always play to their end; a tidier F1 panel with dropdown categories.
  CHANGELOG.md lists the changes of every version.

NEW IN v0.8.5 - PUNISHMENT PROFICIENCY
  The Punish checkbox is now a slider (F1 panel). 0% = the Ghost punishes with the recorded players'
  own punishers and timing. 1-100% = the chance that a punish is PERFECT: the best punish for that
  opening (on block: -10, -12, -13, -14, -15 and slower; lows too), on the first possible frame. Each
  character knows how far its punishers reach and falls back to the next best one when it won't reach.
  Recorded punishes: Jin, Kazuya, Asuka, Devil Jin, Feng, Dragunov, Leo, Claudio, Katarina, Josie,
  Kazumi, Lee, Miguel, Julia (others use the players' own punishers). Default 50%.
  Also better punishing in general: moves that slide or land into a follow-up are seen as still
  recovering, and strings are punished after their last hit.

NEW IN v0.8 - COMBOS
  Combos is now a three-way setting (F1 panel):
    Player's own combo habits        the recorded players' own juggles, drops and all
    Finish combos when appropriate   (default) a full combo route after a launch, ended early when a
                                     hit doesn't connect or a wall is close (no unwanted wall splat)
    Always finish combos             the full route, whatever happens
  Combo routes: Paul, King, Yoshimitsu, Hwoarang, Jin, Asuka, Leo, Claudio, Katarina, Josie, Lee,
  Miguel, Julia - for their usual launchers, counter-hit launchers and low parries. Other launchers
  use the players' own combos.

Requires: Tekken 7 on Steam, latest (final) version, Windows. Offline only - the AI and the gameplay
modifiers are never active online (only the camera and lifebar HUD options, which change nothing but
your own screen, work online too).

INSTALL
  1. Steam > Tekken 7 > right-click > Manage > Browse local files.
  2. Open  TekkenGame\Binaries\Win64  (the folder with TekkenGame-Win64-Shipping.exe).
  3. If a dinput8.dll is already there (another mod), move it somewhere safe first.
  4. Copy the CONTENTS of this package's Win64 folder into that folder:
       dinput8.dll   and the   t7ghost   folder  (t7ghost\combos holds the combo routes and punishes).
  When the game starts, a small note in the top-left corner confirms the mod is loaded.
  Updating from an older version: the mod file is now called dinput8.dll (it used to be
  version.dll) - DELETE the old version.dll from the Win64 folder. While it is still there the new
  dinput8.dll stays switched off and a message box reminds you. Then replace the t7ghost folder's
  profiles, combos, framedata, learned_framedata and walls files; keep your own t7ghost.ini and opponent_*.txt if you
  like (new options use their defaults until you change them; an older t7ghost.ini starts on
  "Custom Settings" with your own values - pick a Ghost AI Preset in F1 to use one).

WHERE THE GHOSTS PLAY
  - PRACTICE: set the opponent (2P) to "CPU" and the CPU level to "ULTRA HARD", pick a character
    with a Ghost as the opponent.
  - ARCADE BATTLE: every opponent with a Ghost is played by its Ghost.
  - TREASURE BATTLE  ->  GHOST BATTLE MODE (see below).
  Each can be switched off in the F1 panel.

GHOST BATTLE MODE  (Treasure Battle reworked - the main menu calls it GHOST BATTLE)
  - Matches use the round count from your Game Options (Treasure Battle normally forces first to 2).
  - A random stage every fight.
  - Opponents are only characters with a Ghost - taken from the game's own ranked ghosts (any rank),
    so they keep their names and custom outfits.
  - END-OF-MATCH MENU after every win or loss, in the game's own result window (controller: up/down,
    left/right, A; keyboard: arrows, Enter):
        Rematch < Same stage | New stage >     same opponent; left/right picks the stage
        Next Opponent: < x >                   left/right to choose one of three different characters
        Choose a specific Opponent and Stage   the game's Simple Select screen: the Ghost and its
                                               costume, your character and costume, and the stage
        Return to Character Select             straight back to Character Select
        Return to Main Menu
  - No treasure chest icons on the match HUD (your rewards still show after the match).
  - Options in the F1 panel (Ghost Battle Mode): Random stage, Only enable Ghost AI Characters,
    Costumes (+ Customization slot to force on Ghosts), End-of-match menu (+ Native look; untick it
    for the overlay version), Call it GHOST BATTLE, Hide treasure chests, Random player per ghost
    (each Ghost imitates one recorded player instead of all of them merged), and Special fights per
    kind (Boss, Turbo, Heist, Double Damage, Aerial Juggle; all off by default).
  Treasure Battle's own rewards and progress are left untouched.

IN-GAME SETTINGS PANEL  (press F1)
  F1 opens and closes the panel at any time, also mid-fight. Use the mouse:
  - AI enabled, Arcade Battle, Treasure Battle
  - Ghost AI Preset     Fair / Medium Challenge / Challenging / Unfair, or Custom Settings (below)
  - Reaction time       how fast it reacts to you (14 frames = strong human; lower = faster)
  - Input read (cheat)  chance per move that it knows your move the instant you press it
  - Guard skill, Decision skill, Adapt to you, Throw breaks
  - Punishment Proficiency   chance of a perfect punish (0% = the players' own punishing)
  - Combos              own habits / finish when appropriate / always finish
  - Wall awareness      sidesteps away from walls, defends corners, presses you into walls
                        (shows what it currently thinks about the walls)
  - Ghost Battle Mode   the options above (Costumes: Treasure Battle's CPU outfits / 1P only / 1P-4P /
                        Presets / your own customizations / Random; customization slot to force)
  - Game Modifiers      No Rage, No Power Crush, No Screw, No Wall Bound, Health 25-500%, Backdash
                        Distance, Sidestep Distance (offline only; immediately, Health per round);
                        Camera Distance, Camera Height Offset, Lifebar HUD X / Y / Size (every mode,
                        online too - only your own screen changes)
  - Imitation Settings  per character: merged (default, all recorded players as one), hybrid, or
                        one recorded player only
  - PLEASE HELP!        how you can help: send play data recorded with the Data Harvesters
  - Reset to defaults / Forget my habits
  Changes take effect immediately and are saved to Win64\t7ghost\t7ghost.ini. The panel shows
  whether the AI is active and which Ghost it is playing as. While the panel is open, the mouse and
  keyboard go to the panel.

THE INI FILE  (Win64\t7ghost\t7ghost.ini - each option is explained right under it)
  Everything in the panel can also be set here. [ui] menu_key changes the panel key (e.g. Insert,
  Ctrl+F1). After editing the file while the game runs, press Ctrl+F9 in the game to reload it.

WHAT THE AI REMEMBERS
  While you play, the AI learns. It writes into the t7ghost folder:
  - learned_framedata.txt   how many frames it really had after blocking moves (this build already
                            includes 2063 such readings)
  - opponent_<number>.txt   your habits per character you play (how often you attack, lows, getups,
                            what it could punish, sidesteps). "Forget my habits" in the panel (or
                            deleting these files) makes it forget you.
  t7ghost.log is a short log of what loaded. Nothing is sent anywhere.

THE PLAYERS
  The Ghosts are built from saved replays and online matches of real players. Players are pseudonyms
  (P01, P02, ...); no names, IDs or other personal information are included.

TROUBLESHOOTING  (no note in the top-left corner, or F1 does nothing)
  First look for  Win64\t7ghost\t7ghost.log  after starting the game once.
  - NO t7ghost.log: the mod was not loaded.
      * dinput8.dll must sit right next to TekkenGame-Win64-Shipping.exe, in
        TEKKEN 7\TekkenGame\Binaries\Win64 - not in the main TEKKEN 7 folder, not in a subfolder.
      * Use the dinput8.dll from "T7 Ghost Mod" - the Data Harvesters' dinput8.dll files have no
        settings panel.
      * Your antivirus may have removed or blocked dinput8.dll (mods that hook the game are often
        flagged). Check that the file is still there after starting the game; allow it if needed.
      * With hidden file extensions, make sure the file is really named dinput8.dll (not
        dinput8.dll.dll).
      * Steam Deck / Linux (Proton): set the game's launch options to
            WINEDLLOVERRIDES="dinput8=n,b" %command%
      * Another mod also needs the name dinput8.dll: the mod works under the name version.dll too -
        rename it and start the game again.
  - The log says "unsupported game build": the mod only works with the latest Steam version of
    TEKKEN 7. Update the game, or verify its files in Steam (Properties > Installed Files). A message
    box says so when the game starts.
  - The log says "could not hook Present" or "overlay: could not start": another overlay or graphics
    tool is in the way (ReShade, RivaTuner / MSI Afterburner, Special K, ...). Turn it off and restart
    the game. A message box says so when the game starts.
  - The log says "overlay: ready" but F1 does nothing: on many laptops F1 needs the Fn key (Fn+F1),
    or another program may use F1. You can pick another key: [ui] menu_key in t7ghost.ini
    (e.g. Insert or Ctrl+F1).
  Still stuck? Report it on the GitHub page with your t7ghost.log - it only lists what the mod loaded.

UNINSTALL
  Delete dinput8.dll and the t7ghost folder from the Win64 folder (put back any dinput8.dll you moved).

NOTES
  - Made with AI assistance (Anthropic's Claude) under t7ghostauthor's direction.
  - Not affiliated with or endorsed by Bandai Namco. Tekken is a trademark of Bandai Namco
    Entertainment Inc.
  - Uses MinHook (BSD 2-clause) and Dear ImGui (MIT) - see THIRD_PARTY_LICENSES.txt.
  - Updates: https://github.com/NobodyCatchMe-AZ92/T7-Ghost-AI or tekkenmods.com
