T7 Ghost Harvester v0.6  (by t7ghostauthor)
==========================================

Thank you for helping! This records Tekken 7 fight data so a replay-trained AI can learn how real
players play. It ONLY READS the game's fight state: it does not change inputs, does not hook the
game, has no AI in it, and never writes into the game or your save files.

Requires: Tekken 7 on Steam, latest (final) version. Windows.

There are two versions. Use ONE at a time (both are called dinput8.dll, like the T7 Ghost Mod
itself - installing one replaces the other):

  Replay Harvester  - records your saved replays while you watch them in the Battle Log.
                      Output: TEKKEN 7\TekkenGame\Binaries\Win64\t7GhostReplayData\
  LIVE Harvester    - records real matches while you play or spectate them (online or offline VS).
                      It does not record replays. (Practice gets recorded too and is filtered
                      out later - you can leave it installed.)
                      Output: TEKKEN 7\TekkenGame\Binaries\Win64\t7GhostLIVEplayData\

WHAT TO RECORD (please follow)
  The data teaches an AI how PEOPLE play, so only human players should be recorded:
  - YES: online matches (ranked, player match), spectating online matches, and offline matches
         between two people.
  - NO:  offline fights against the CPU (Arcade, VS CPU, Treasure Battle, Story...). With the LIVE
         Harvester installed, those get recorded too - please don't send them (or remove the
         LIVE Harvester before playing against the CPU).
  - Replays are always fine: saved online replays are two human players.

INSTALL
  1. Open your game folder: Steam > Tekken 7 > right-click > Manage > Browse local files.
  2. Go to  TekkenGame\Binaries\Win64  (the folder with TekkenGame-Win64-Shipping.exe).
  3. If a dinput8.dll is already there (another mod, or the T7 Ghost Mod), move it somewhere safe
     first.
  4. Copy dinput8.dll from "Replay Harvester" (or "LIVE Harvester") into that folder.
     Used a v0.5 harvester before? Those were called version.dll - delete that file (while it is
     there the new harvester stays switched off and a message box reminds you).
  5. Start the game. The output folder appears next to the exe.
     Nothing appears? See "IF NOTHING HAPPENS" below.

BEFORE YOU START (important for the Replay Harvester)
  Turn OFF anything that pauses replays, or the harvest stops / the data gets cut up:
  - the replay tips that pop up during playback ("Recommended Combo", "Recommended Punishment"
    and similar Replays & Tips hints) - switch them off in the replay / Replays & Tips options
    before you start.
  - anything else that pops up over a replay (notifications, overlays that steal focus).
  Run the game in WINDOWED or BORDERLESS mode: the automatic mode looks at the game window to see
  when a replay has ended, which may not work in exclusive fullscreen.

USE - Replay Harvester, automatic (recommended)
  1. Open Battle Log > Replays and highlight the FIRST replay you want to harvest (do not open it).
  2. Press Ctrl+F8. You hear a short "ding": automatic mode is on.
     It now repeats by itself: plays the highlighted replay (left side), waits until the replay is
     over, returns to the Battle Log, moves one entry down and plays that one.
  3. After the last replay the Battle Log wraps to the top, so it keeps going round the list.
     That's fine - repeated replays are recognised and removed when the data is processed.
     Press Ctrl+F8 again to stop it (you hear an error-style beep) once it has been through
     your list, or whenever you like.
  Keep the game window in front while it runs - it only presses keys while the game is the active
  window and simply waits if you switch away. Don't touch the keyboard/controller while it runs.
  harvest_log.txt in the output folder shows what it did ("auto replay: ...").

USE - Replay Harvester, by hand
  Battle Log > Replays: play each replay to the end (normal speed, one after another). One file is
  written per replay (rep_<date_time>.jsonl). Don't skip or fast-forward through rounds.

USE - LIVE Harvester
  Just play (ranked, player match, offline VS) or spectate. One file per match (live_<date_time>.jsonl).

SEND THE DATA
  Zip the output folder (t7GhostReplayData or t7GhostLIVEplayData) and e-mail the zip to:

      DreamOfCasinoSoul@proton.me

  That is all. (Too big for an e-mail attachment? Upload it to a file sharing service and e-mail
  the download link instead.)
  harvest_log.txt inside it is a short log (dates, characters, frame counts) - fine to include.

PRIVACY
  Player names are never saved. They are turned into short codes using a random key that is
  created on YOUR PC in  Win64\t7GhostHarvest_private\ . Nobody can turn the codes back into
  names without that key - so do NOT send the t7GhostHarvest_private folder.
  Data you send becomes part of the public Ghost dataset, so anyone can keep improving the Ghosts:
  the codes are removed first and every player becomes a number (P01, P02, ...). The published
  recordings hold gameplay only - moves, positions, health, inputs - nothing about you, your PC or
  your Steam account.

IF NOTHING HAPPENS
  - On some Windows installations, or when another mod already uses the name dinput8.dll, rename the
    harvester's dinput8.dll to version.dll - it works under either name.
  - Steam Deck / Linux (Proton): set the game's launch options to
        WINEDLLOVERRIDES="dinput8=n,b" %command%

UNINSTALL
  Delete dinput8.dll from the Win64 folder (put back any dinput8.dll you moved in step 3).
  The output folders can be deleted any time.

Online use is at your own risk: the harvester only reads memory (like the well-known TekkenBot
overlay did) and changes nothing in the game, but it is still a third-party DLL.
