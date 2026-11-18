---
id: BUG-1263
title: Cell: the host's screen is black behind its evolution vote until every guest pressed I'm ready (up to 60 s)
status: fixed
kind: feature
stage: Cell
reported: 2026-11-18
source: fix/w53-cellvote (BUG-1245 follow-up)
role: host
versions: 0.7.15.1
fixed_in: unreleased
branch: fix/w53-cellnow
commits: c0ac4ab0 22b0acda 2774e956
tests: evo_screens_cell_noready, evo_screens_cell
signature:
docs: docs/re/cell.md#cell-editor-now, docs/06-testing.md#testing-cell-editor-now, docs/09-coop-design.md
related: BUG-1245, BUG-1239, BUG-0516, BUG-0315, BUG-1264
switch: cell.editor_now
---

## Report
Found on fix/w53-cellvote while fixing BUG-1245: with the repeated mating rounds gone, the host's Cell evolution vote
shows one continuous black picture (the game's fade after the egg laying) behind the co-op window until every guest
pressed I'm ready or the 60 s ran out (evo_screens_cell_noready: 54 s black, out/lab/20261118-131528). Before BUG-1245
the repeated mating scenes drew the pool every ~10 s and hid it (the "flicker" of the players' video). The players'
"black screen for 25+ s" is this wait.

## Root cause
By design (docs/09-coop-design.md 9.3, BUG-1239): Cell keeps the evolution vote; h_Submit holds the host's request and
the game has faded out already. Observed (docs/re/cell.md#cell-editor-vote-timing): the Cell vote moves only the host:
a guest's I'm ready opens nothing for the guest, and after the host's editor nothing is reloaded (BUG-0508). So the
wait gives the guests nothing but costs the host up to 60 s of black.

## Fix
Done (fix/w53-cellnow): `cell.editor_now` (vote.cpp h_Submit, with the session setting `core.editor_now`, default on):
the host's Cell request goes to the game at once (only the share-save / new-body holds stay), every player gets the
notice "<host> evolves its cell"; no offer (a guest evolves through its own mating call). The vote protected nothing in
Cell (docs/re/cell.md#cell-editor-now); BUG-0315 / BUG-0178 are about the stage switch, which keeps its vote. With the
setting off the 0.7.15 vote runs (cell.vote_mate and core.vote_guest_editor still apply). Original sketch: not in a
patch release (a design change of 9.3 and of the tests built on the Cell vote: cell_editor_vote*,
evo_screens_cell*, cell_editor_budget). Sketch: treat the Cell mating evolution like Creature's (BUG-0516
core.editor_now): the host's request goes on at once (no vote, no hold, no black), the guests get a notice "<host>
evolves its cell" (no offer: a guest evolves through its own mating call); keep the vote only for stage starts
(Cell -> Creature, BUG-0315). A switch `cell.editor_now` (off: the vote of 0.7.15). Alternative, smaller: undo the
game's fade while the vote holds so the host sees its pool (its cell is locked by the mating meanwhile); the fade's
code (cinematic manager 0xAD7DC0 calls in 0xE72AC0 / 0xE77930, the effect 0xA1B0293D) is not traced.

## Verification
Reproduce: `lab.py run evo_screens_cell_noready` (the host's screen row "host screen while its vote runs": black from
the vote's start to its end). The test now accepts the black up to the vote's end (BUG-1245); a fix would turn that
check back into "not black for 20 s".

* FAIL: out/lab/20261118-134724 (`SPORE_FIX_OFF=cell.editor_now`, lane 2, evo_screens_cell_noready): the host's
  editor 59 s after its mate, 54 s black behind the vote window (frame 01_host_vote_host.jpg).
* PASS: out/lab/20261118-134729 (lane 3, evo_screens_cell_noready and evo_screens_cell) and out/lab/20261118-135228
  (lane 2, noready again, measured from the mating call): the editor 0 s after the mate, longest host black 0 s, the
  guest's notice and drawn pool (frames 01_host_vote_host / _guest).
* Related, with the vote path (`--vote`: the setting off): evo_screens_cell_eggs, evo_screens_cell_both OK; switch_vote,
  cell_editor_budget OK (all out/lab/20261118-135228). editor_desync fails at step 5 also on the base rel/0.7.15.2
  (BUG-1264, not this change); its Cell step 2 passes.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellnow): fixed: the host's Cell editor opens at once (cell.editor_now).
