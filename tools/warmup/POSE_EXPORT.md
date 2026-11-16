# Turning a character's intros / win poses into warmup animations

src/warmup.hpp, "pose preview" / "pose export" sections (in every build since 2026-11-16). Used 2026-11-15 / 16 for
Katarina (Up, Final), Josie (Round1, Final), Gigas (Round1, Down, Final), Lei (Round1, Final) and Ganryu (Round1, Final).

## 1. Record the candidates (automatic)

The mod records them as the game plays them, into `Win64\t7ghost\pose_moves.txt`
(`<char id> <intro|win> <move index> <name> <frames>`):

- **Intros** - every move a fighter goes through before Round 1 (round 0, phases 0 / 1). An intro can be several moves.
  Intros are random: rematch a few times (the intros can be left right after "Round 1" - no need to fight).
- **Win poses** - every move the ROUND WINNER goes through after the KO (wins counted at the call phase, because the
  KO adds the win while the phase is still 4). Win rounds / a match with the character.
- KO / death moves (`fatal`, `death`) left over at a rematch are skipped.

Char ids: Katarina 21, Josie 24, Gigas 25 (see `char_file_name` in warmup.hpp for the rest).

## 2. Preview (Practice / any offline battle, no Ctrl)

F10 next, F9 previous, F11 again; Shift = P2's character. A label at the bottom shows
`P1 <Char> - pose N / M - INTRO|WIN POSE - move <index> <name> - <frames>`; every preview is also in `t7ghost.log`
("pose preview:"). The author picks by POSE NUMBER - map it to the move index through that log (numbers shift when
new moves are recorded; intros first, then win poses, in recording order).

## 3. Export

`Win64\t7ghost\pose_export.txt`: one line per pick, `<Character> <Round1|Up|Down|Final> <move index>`.
Optional fourth word: save it as another character, e.g. `Lars Round1 1234 Noctis` writes `Noctis_Round1` from Lars's move
(the export runs with the performing character - Lars - on P1).
In Practice with the character on P1 (Shift = P2) press **F12**: each line is played (hands captured live), the body
decoded with the game's decoder and written as `warmup\<Character>_<Sit>.bin` + `_HandL` / `_HandR`. An existing file
is kept as `.bak`. Leave the dummy alone until the label says "export done".

Checks built in: a conversion self-check on `warmup\Jin_Round1.bin` first (positions must come out as positions -
an earlier version wrote the body position as angles); rotations kept on one angle branch frame to frame
(`branchfix.py` does the same to an existing file).

Afterwards: copy the new files to `dist/warmup` for the next release (they come from the game, not an FBX -
`batch.py` never rebuilds them) and check the log ("pose export: wrote ...", hands captured N / N frames).

## 4. Tune (tuning.txt, Ctrl+F9)

They are long (250-320 frames vs ~171 for "Round N ... Fight!"): `cutstart`, `cut`, `blend`, and `pin=1` - the game's
own animations carry their walking on bone 0 (root offset), which pin holds at 0 (since 2026-11-15).
