---
id: BUG-9266
title: Cell: a click on History's check that waits (own progress, the switch vote) leaves the screen white until the stage moves on
status: fixed
kind: bug
stage: Cell
reported: 2026-11-20
source: own progress play test 2026-11-20
role: host, guest
versions: 0.7.15.2
fixed_in: unreleased
branch: feat/own-progress
commits: d066eb71
tests: own_progress_cell
signature: Cell_EndSequence 0xE72C80
docs: docs/re/cell.md#cell-end-sequence
related: BUG-1269, BUG-9265, BUG-0315
---

## Report
Own progress play test (lane B, 2026-11-20): a player whose Cell bar is full clicks History's check before the
partner's bar is full; the click is held (the host's) or refused (a guest's) and that player's screen stays plain white
(the Cell-end fade) until the stage moves on: 26 s and more in the test, as long as the partner needs. Same picture as
BUG-1269 (the host's white screen during the switch vote) and the guest's refused click of BUG-0315.

## Root cause
Observed (objdump of the unpacked .text of the running Steam game, docs/re/cell.md#cell-end-sequence): History's check
(`Cell_HistoryCheck` 0xE75B80) runs `Cell_EndSequence` 0xE72C80 when the stage is done (G+0x5158 == 5): the white screen
(0xE4F640(1), inferred), the walk-out animation 0x2E and a timed event 0x1F after it, which calls CellStage_ToCreature
0xE62440. The mod held or refused the switch only there, after the sequence had whitened the screen; nothing undid it
until the stage moved on.

## Fix
`coop.fix cell.end_click` (vote.cpp h_CellEndSeq, a hook on `SporeCoop::Cell_EndSequence`): a click that must wait is
taken before the sequence. The host's (own progress, not every bar full): kept (switch_due), History closes, the host
plays on; once every bar is full the mod runs the original sequence outside the frame, and the switch and its vote follow
as from the game's own click. A guest's click in the host's Cell world (own progress, or the switch vote of BUG-0315): its
I'm ready during the vote, otherwise the "you move on together" notice; never the sequence. Off: the sequence runs and
the switch is held after it (0.7.15).

## Verification
Lane B, Windows, Steam build, new Cell games each run (`white` = share of near-white pixels of the screenshot):
- `coop.fix cell.end_click off` (the twin): the host's early click left its screen 0.69 white 10 s later; the guest's
  click on History's check during the vote 0.52 white. The bug reproduced.
- on: the host's early click 0.00 white (the pool drawn, "Advance to Creature" button), the host plays on, no vote; the
  guest fills, the kept click runs the sequence and the vote starts by itself; the guest's click in the vote 0.00 white
  and counted as its I'm ready; both games in Creature. Guest first (on): the guest's early click 0.00 white, still 0.00 30 s later, no vote; the host fills, its check starts
  the vote, both games in Creature.
- no-fail-run: the Linux lab is not available here; FAIL (switch off) and PASS (on) were run by hand in the game on
  lane B as above, with the same steps as the lab test; the orchestrator adds the lab run ids.
- Lab: tools/tests/coop_own_progress.py --part cell --first host|guest checks the white share of the early click's
  screenshot (< 0.5); SPORE_FIX_OFF=cell.end_click fails it. Not run in the Linux lab here.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on feat/own-progress (cell.end_click), twin run white 0.69 / 0.52, fix 0.00 / 0.00.
