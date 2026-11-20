---
id: BUG-1245
title: Cell: the host's mate call while the vote holds: black screen without the vote window, the mating scene and egg laying repeat (6+ eggs)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in: 0.7.15.2
branch: fix/w53-cellvote
commits: fb58d6c8 707817ca 9b1dca20 3c49737f dd36d05f
tests: evo_screens_cell_eggs, evo_screens_cell_eggs_lowfps, evo_screens_switch_slow
signature: host log "cell: DNA at the editor request" every ~10 s while "vote=on" in Cell; or the host Creature/black for 60 s behind the arrival vote of editor FD4902BD
docs: docs/re/cell.md#cell-mating-round
related: BUG-1239, BUG-1240, BUG-0315
switch: cell.vote_mate, core.arrival_nowait
---

## Report
A players' video (the guest had just called a mate and its editor opened at once). The host calls a mate: the host's screen fades to black for 25+ s ("where am I, I have no editor"), sometimes with no vote window over the black; every few seconds the black flickers and the cells mate and lay eggs again (6 eggs in a few seconds). The same twice more in the session.
A second analysis of the video suggests: vote.cpp's Submit hook holds the host's EditorRequest during the vote while the Cell stage has already started its fade and, with the mate still near, replays the mating scene and the egg laying (0xE78D10 -> Submit). Likely the same start as BUG-1239 (the guest in its own editor); the egg loop and the missing vote window are to be checked on 0.7.15 and with the BUG-1239 fix, also for a plain vote (the guest not in an editor).

## Root cause
Observed (Ghidra GOG 2024; lab lanes 2 / 3, `coop_evo_screens.py --part cell-eggs`, the BUG-1239 fix in the base):
* The egg loop is a plain vote's, not only the BUG-1239 case: while the host's vote holds its Cell editor request,
  the avatar and the mate stay within the meeting distance, and the cell step's meeting 0xE78D10 runs the whole round
  again every ~10 s: mating scene 0xE75EA0, egg laying 0xE77930, the editor request 0xE72AC0 -> 0xE59800 (refused by
  h_Submit: one vote per editor). Lab 20261118-122608 / -123203 (`cell.vote_mate` off): 4 more mating scenes and 5-6
  more egg layings in 46 s, and 3 flashes of the drawn pool behind the vote window in 19 host shots (the "flicker").
  The host log of the BUG-1239 run 20261118-094043 (evo_screens_cell_noready) already had a request every 9.7 s for
  the vote's 60 s. With the guest in its own editor the BUG-1239 fix ends the vote at once (no loop there); the loop
  needs a guest that answers late or never.
* The same black-behind-the-vote at the Creature start (coordinator's evidence, evo_screens_switch_slow failing on
  rel/0.7.15.1): BUG-0315's arrival vote holds the landfall editor until every guest arrived; over 512 kbit/s the host
  was black 62 s (lab 20261118-123948, `core.arrival_nowait` off: "host editor at 62 s").
* Not reproduced: the missing vote window. The host's co-op window stayed drawn in 15 of 15 looks during the vote,
  with the fix on and off, also at 10 fps (frames host_vote_1 / _2). Inferred: the players closed it (red cross) or
  read the flashes as its absence.

## Fix
One rule for both (vote.cpp vote_tick: what a vote that holds the host's editor does meanwhile):
* `cell.vote_mate`: while the vote holds the host's Cell request, the mate index G+0x51D4 is forgotten (the cell stays;
  restored if the vote is dropped while the stage goes on): the round plays once. `coop.vote` counts it; `coop.pool
  mate` prints `matings=` / `egg_lays=` (the avatar's animation entering 0x42 / 0x29).
* `core.arrival_nowait`: the arrival vote does not wait for a guest still on its way whose build takes its own landfall
  editor on arrival (BUG-1240; its creature family minor >= 1, bumped here); an older guest is still waited for.
Off: as 0.7.15.

## Verification
* `evo_screens_cell_eggs` (cell fixture, real clicks: the host's mating call and swim, the guest's I'm ready after
  45 s): FAIL out/lab/20261118-122608 and 20261118-123203 (`SPORE_FIX_OFF=cell.vote_mate`, lane 2: 4 more matings,
  5-6 egg layings, 3 flashes); PASS out/lab/20261118-122633 and 20261118-123223 (lane 3: 0, 0, 0 flashes; the host's
  editor, its return, both pools drawn).
* `evo_screens_switch_slow` (SPORE_NET=lowband): FAIL out/lab/20261118-123948 (`SPORE_FIX_OFF=core.arrival_nowait`,
  lane 2: the host black 62 s); PASS out/lab/20261118-124008 (lane 3: the host's editor 10 s after the switch, longest
  black 12 s, the guest's own editor on arrival, two own creatures). Both runs also fail the lab's sync-check gate
  (Cell objects missing on the guest under lowband: in every run of this test since it exists, not this ticket) and
  BUG-1255's dialog check (fixed later on this branch).
* `evo_screens_cell_eggs_lowfps` (the host at 10 fps, the vote runs out): FAIL out/lab/20261118-130445 (lane 2,
  `SPORE_FIX_OFF=cell.vote_mate`: 5 more matings, 6 egg layings, 3 flashes in 58 s); PASS out/lab/20261118-131528
  (lane 2).
* Related, kept: evo_screens_cell_both, cell_editor_budget OK (out/lab/20261118-125348); evo_screens_cell, switch_vote
  OK (out/lab/20261118-125715). With the flicker gone, evo_screens_cell_noready shows the host's black behind a vote
  nobody answers as one 54 s run: the vote's own wait (BUG-1263, open with a design sketch), the test accepts it up to
  the vote's end: OK out/lab/20261118-132953. evo_screens_switch_noready failed once on the guest's stage-entry black
  (22 s, the same timeline as before the fix: the guest arrives before the host's editor) and passed on its rerun
  out/lab/20261118-133008; evo_screens_switch and _lowfps OK (out/lab/20261118-131548).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellvote): the egg loop reproduced on a plain vote (cell.vote_mate) and the landfall arrival wait (core.arrival_nowait) fixed; the missing vote window not reproduced.
- 2026-11-18: released in 0.7.15.2.
- 2026-11-20: the no-wait applied to every switch (every guest is on its way when the landfall editor asks), not only slow links: BUG-1336 (`core.arrival_wait`) waits again for a quick arrival, fix/w59-cell.
