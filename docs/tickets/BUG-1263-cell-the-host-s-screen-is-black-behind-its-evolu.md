---
id: BUG-1263
title: Cell: the host's screen is black behind its evolution vote until every guest pressed I'm ready (up to 60 s)
status: open
kind: feature
stage: Cell
reported: 2026-11-18
source: fix/w53-cellvote (BUG-1245 follow-up)
role: host
versions: 0.7.15.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1245, BUG-1239, BUG-0516
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
Not in a patch release (a design change of 9.3 and of the tests built on the Cell vote: cell_editor_vote*,
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

## History
- 2026-11-18: opened.
