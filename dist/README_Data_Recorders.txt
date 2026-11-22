T7 Ghost AI - Data Recorders  (by t7ghostauthor)
==================================================

Thank you for helping! The Data Recorders record Tekken 7 fight data so the Ghosts can learn how real
players play. They ONLY READ the game's fight state while recording: they do not change inputs and
never write into the game or your save files.

Since v1.4 the Data Recorders are built into the T7 Ghost AI mod - there are no separate recorder files
to install any more. Open the mod's panel (F1) > Data Volunteering (optional):

  LIVE Match Data Recorder   - records ONLINE matches while you play or spectate them (ranked,
                               player match). Offline fights - against the CPU, Practice, offline
                               VS, Ghost fights - are never recorded. Replays are not recorded.
                               Output: TEKKEN 7\TekkenGame\Binaries\Win64\t7GhostLIVEplayData\
  Replay Data Recorder       - records your saved replays while you watch them in the Battle Log.
                               Output: TEKKEN 7\TekkenGame\Binaries\Win64\t7GhostReplayData\

Only one Data Recorder can be on at a time; both are off by default. Your choice is remembered.
Used the old standalone recorders (a separate dinput8.dll)? Remove that file - the mod does the same
now, writes to the same folders and keeps the same private key.

WHAT GETS RECORDED
  The data teaches an AI how PEOPLE play, so only human players are recorded: the LIVE Match Data Recorder
  records online matches only, and saved online replays are two human players. You can leave the
  LIVE Match Data Recorder on while you play offline - nothing offline is recorded.

BEFORE YOU START (important for the Replay Data Recorder)
  Turn OFF anything that pauses replays, or the recording stops / the data gets cut up:
  - the replay tips that pop up during playback ("Recommended Combo", "Recommended Punishment"
    and similar Replays & Tips hints) - switch them off in the replay / Replays & Tips options
    before you start.
  - anything else that pops up over a replay (notifications, overlays that steal focus).

USE - Replay Data Recorder, automatic (recommended)
  1. F1 > Data Volunteering (optional): tick "Replay Data Recorder".
  2. Open Battle Log > Replays and highlight the FIRST replay you want to record (do not open it).
  3. Open F1 and click "Start Replay Data Gathering" (the panel closes), or press Ctrl+F8 on the
     Battle Log. You hear a short "ding": automatic mode is on.
     It now repeats by itself: plays the highlighted replay (left side), waits until the replay is
     over, returns to the Battle Log, moves one entry down and plays that one.
  4. It stops by itself after the last replay (when the list comes round to a replay it already
     recorded in this run - that repeat is not kept). Press Ctrl+F8 (or "Stop Replay Data
     Gathering" in F1) to stop it earlier - you hear an error-style beep.
  The game does NOT need to stay in front: you can use other programs meanwhile (the run presses
  the game's own menu buttons, not your keyboard). Avoid pressing buttons in the game while it runs.
  DataRecorder_log.txt in the output folder shows what it did ("auto replay: ...").

USE - Replay Data Recorder, by hand
  With "Replay Data Recorder" ticked: Battle Log > Replays, play each replay to the end (normal
  speed, one after another). One file is written per replay (rep_<date_time>.jsonl). Don't skip or
  fast-forward through rounds.

USE - LIVE Match Data Recorder
  Tick "LIVE Match Data Recorder" and just play online (ranked, player match) or spectate online
  matches. One file per match (live_<date_time>.jsonl).

SEND THE DATA
  Zip the output folder (t7GhostReplayData or t7GhostLIVEplayData) and e-mail the zip to:

      DreamOfCasinoSoul@proton.me

  That is all. (Too big for an e-mail attachment? Upload it to a file sharing service and e-mail
  the download link instead.)
  DataRecorder_log.txt inside it is a short log (dates, characters, frame counts) - fine to include.

PRIVACY
  Player names are never saved. They are turned into short codes using a random key that is
  created on YOUR PC in  Win64\t7GhostDataRecorder_private\ . Nobody can turn the codes back into
  names without that key - so do NOT send the t7GhostDataRecorder_private folder.
  Data you send becomes part of the public Ghost dataset, so anyone can keep improving the Ghosts:
  the codes are removed first and every player becomes a number (P01, P02, ...). The published
  recordings hold gameplay only - moves, positions, health, inputs - nothing about you, your PC or
  your Steam account.

IF NOTHING HAPPENS
  - Check that the mod itself works (F1 opens the panel) - see the troubleshooting part of README.txt.
  - The output folder appears the first time a Data Recorder is switched on; DataRecorder_log.txt in it says
    what was recorded.

TO STOP
  Untick the Data Recorder in F1. The output folders can be deleted any time.

Online use is at your own risk: the Data Recorders only read memory (like the well-known TekkenBot
overlay did) and change nothing in the game, but the mod is still a third-party DLL.
