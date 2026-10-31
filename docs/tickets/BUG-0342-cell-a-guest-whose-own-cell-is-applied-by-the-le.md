---
id: BUG-0342
title: Cell: a guest whose own cell is applied by the level rebuild at the world load is a ghost (cannot eat, bite or be hit)
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (host and guest marks)
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creations.md (4.2 Cell stage), docs/re/cell.md#cell-rebuilds-in-session
related: BUG-0345, BUG-0341
---

## Report
Host on GOG 2024, guest on Steam (both Windows, 0.7.3.1), a new world in Cell. Marks of both
players in the same session: the host "the guest got its own cell from another world and is
basically a ghost which cannot interact with anything" (about 5 min after the load), later "my
friend is a ghost"; the guest "I was a ghost (go through everything, can't eat, can't attack or
get attacked)".

## Root cause
Observed (guest log, 10 s `coop: cell:` lines, `creations:` lines):
* Ghost periods are exactly the times after a world load at which the guest's own cell (from its
  choices.ini or its newest own creation, `creations.cpp apply_own_cell`) was applied with
  `own cell is now <key> (level rebuilt)`. That line comes in the same millisecond as
  `stage: Loading -> Cell`, i.e. RebuildLevel is requested (G+0x51D8 = 1) on the first frames of a
  freshly loaded Cell game.
  * load 1 (own cell applied by the rebuild): 10 minutes with `ate=0 bites_sent=0` in every 10 s
    line, also with plants 0-1 m away (`nearest plant=0..1`, `plants=43/61` on screen); sync-check
    MISMATCH twice (38 and 57 cells apart).
  * load 2 (the guest's cell key was already the one applied, so no rebuild, the game kept the
    world's model, `(DIFFERENT)` in the trace): normal play, eats and bites from the first minute.
  * load 3 (a new own cell from the guest's editor applied by the rebuild again): ghost again for
    about 4 minutes (the marks), no eat, no bite.
  * after the guest's own cell editor (the game's own RebuildLevel, same model): eats and bites
    again at once.
  * a later new world: the same rebuild at the load; the report was made 50 s later (too short to
    tell, `ate=0` meanwhile).
* The lab test `coop_creations_cell.py` chooses the own cell after the guest is in Cell, waits
  12 s and checks the keys and the mirror count only, not that the cell can eat, bite or be hit.

Inferred: the rebuild requested on the first Cell frames of a load leaves the new avatar without
its interaction (collision/abilities): the editor's own path refreshes the unlocked parts
(`S+0x34`, 0xE4FBA0) before RebuildLevel and is not run on the first frames of a load;
`apply_own_cell` does only `S+0x10` + PreloadModel + the flag. Not checked which of the two
(timing or the missing part refresh) is the cause.

## Fix
Not done yet. Candidates, behind `coop.fix cell.own_rebuild` (off = today):
* apply the own cell only after the Cell game has run some frames (as `apply_own_avatar` waits
  3 s for creatures), and/or
* do what the editor's exit handler does before the rebuild (the unlocked parts refresh
  0xE4FBA0), then RebuildLevel.

## Verification
Test to write (lanes 5/6 after the go-ahead): `coop_cell_own_load.py`: the guest has an own cell
chosen (premade BAF1AD40 as in coop_creations_cell.py, written to the test profile's choices
before the join), the host loads a Cell world, the guest gets it; then, on the guest, the game's
own state: it eats a plant put next to it (food rises in the game's progress, not our counter),
an NPC bite lowers its hp, and the host's puppet of it collides. Must FAIL on main.

## History
- 2026-10-31: opened from the host's and guest's report zips (marks, cell trace lines).
