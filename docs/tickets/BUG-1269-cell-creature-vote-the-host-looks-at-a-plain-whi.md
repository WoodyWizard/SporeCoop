---
id: BUG-1269
title: Cell -> Creature vote: the host looks at a plain white screen (the Cell-end white fade held) while the guest still sees the cinematic
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: key-frame review
role: host
versions: 0.7.15.1
fixed_in: 0.7.16
branch: fix/w54-visual
commits: 849bcc13
tests: evo_screens_switch, evo_screens_switch_lowfps, evo_screens_switch_noready, evo_screens_switch_slow
signature:
docs: docs/re/cell.md#cell-end-white
related: BUG-0315
---

## Report
Key-frame review (0.7.15.2 final check, lab 20261118-144132, sheets 048/050/052/054 `*_01_switch_vote`): during the
switch vote at the end of the Cell stage the host's screen behind the co-op lobby is plain white (the game's
Cell-end fade to white, held while the vote waits), the guest's shows the Cell-end cinematic ("Congratulations!
You're on the path to sentience!"). The same in 0.7.15.1 (lab 20261118-110806: the white share of the host's frame
0.46 in all four variants). The tests pass (they check for black, not white).

## Root cause
Static (Ghidra, GOG 2024) and the logs of lab 20261118-144132; no lab run yet (owner rule 2026-11-18).
docs/re/cell.md#cell-end-white.
* History's check (UI handler 0xE75B80) runs the Cell stage's end sequence `SporeCoop::CellStage_End` 0xE72C80:
  G+0x5158 = 3, the HUD hidden, the letterbox (0xE4F640(1)), the avatar's animation 0x2E and an event 0x1F after it.
  The event's dispatch (0xE7E130 case 0x1F, return address 0x00E7E36E) calls `CellStage_ToCreature` 0xE62440.
* `core.switch_vote` (BUG-0315) held ToCreature, i.e. the END of that sequence: the host's game stayed in state 3 with
  the letterbox and the sequence's last picture for the whole vote. The host's log: "the host's switch to the
  Creature stage (from 00E7E36E, cinematic 0)" 16 s after its cinematic ended, then the vote up to 27-60 s. The key
  frames show plain white between the bars. Inferred: the white is the sequence's last picture (the animation 0x2E);
  in the game's own flow the mode switch follows at once. The guest, whose own Cell-end cinematic started later, was
  still in it.

## Fix
`core.switch_vote_world` (vote.cpp h_CellStageEnd on `SporeCoop::CellStage_End`, switch_hold shared with
h_CellToCreature): the vote holds History's check before the end sequence; the host's game stays in state 5 and the
host plays on in its pool while the others vote. When the vote passes the action runs the end sequence (g_end_go) and
its event 0x1F reaches h_CellToCreature with g_switch_go set: the game's own switch; when the sequence does not start
(G+0x5158 not 3 after it) the switch runs at once as before. A guest's check in the host's world is its I'm ready
before the sequence too (its game no longer starts its own end sequence there). `coop.fix core.switch_vote_world off`:
held at the switch as in 0.7.15. `coop.vote` prints "host History checks held before the end sequence N".

## Verification
Pending the night run (no lab runs allowed while the fix was written). `coop_evo_screens.py --part switch` (all four
variants) now measures the host's picture during the switch vote like a key frame: the share of near-white pixels
(grey >= 240) beside the co-op window in the key frame `switch_vote` and the median of the vote's shots must stay
under 0.5; the guest's share is printed beside it. The key frame's note says what each screen must show.
* The bug on the code before the fix (no check yet then, the picture only): out/lab/20261118-144132 (rel/0.7.15.2) and
  out/lab/20261118-110806 (rel/0.7.15.1), frames `evo_screens_switch*/01_switch_vote_host.jpg`.
* FAIL and PASS runs of the new check: not run yet, the commands below (the night run).
Tonight (lanes 2 and 3, build first with `make -j8`):
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch evo_screens_switch_noready --lane 2 --retry 0 --no-build`
* FAIL expected (the twin): `SPORE_FIX_OFF=core.switch_vote_world SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch --lane 3 --retry 0 --no-build`
* regression: `SPORE_DETECTOR=20 python3 tools/lab.py run switch_vote switch_vote_off evo_screens_switch_lowfps evo_screens_switch_slow --lane 3 --retry 0 --no-build`
* frames: `python3 tools/frames_review.py <run> --test evo_screens_switch` (the host's frame: its pool behind the window)

## History
- 2026-11-18: opened; pre-existing, not a blocker of 0.7.15.2; planned for 0.7.16.
- 2026-11-18: root cause from Ghidra and the logs (fix/w54-visual); fix `core.switch_vote_world` and the check written; lab pending the night run.
- 2026-11-23: released in 0.7.16.
