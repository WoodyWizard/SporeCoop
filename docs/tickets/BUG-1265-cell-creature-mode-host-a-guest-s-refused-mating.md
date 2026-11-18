---
id: BUG-1265
title: Cell, creature.mode host: a guest's refused mating call replays the mating scene and the black fade every ~10 s
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: fix/w53-cellnow
role: guest
versions: 0.7.15.1
fixed_in:
branch:
commits:
tests: evo_screens_cell_one
signature: guest log "creature.mode host: this guest's editor request is refused" every ~10 s in Cell
docs:
related: BUG-1245, BUG-1263, BUG-0769, BUG-0920
switch: cell.refused_mate
---

## Report
Found on fix/w53-cellnow (evo_screens_cell_one, out/lab/20261118-140510): with the session setting creature.mode host
(one creature for everyone, only the host evolves) a guest's own mating call in Cell is refused, as designed, but the
guest's screen then goes black again and again: black 26 s, drawn 2 s, black 8 s, drawn 2 s, ... for the test's 70 s.

## Root cause
Observed (guest log of 20261118-140510): "creature.mode host: this guest's editor request is refused (the host evolves)"
every 9.7 s. h_Submit refuses the request, but the guest's mate G+0x51D4 stays next to its cell, so the cell step's
meeting 0xE78D10 plays the mating scene, the egg laying with the game's fade (0xE77930) and the request again
(docs/re/cell.md#cell-mating-round): the loop of BUG-1245, on a refused request instead of a held one. Not BUG-1263: the
`one` branch runs before cell.editor_now and only on the guest (switch-off run below).

## Fix
`cell.refused_mate` (vote.cpp): the refusal in Cell forgets the mate on the next frame (evo_flag_tick,
cellsync::forget_mate, outside the game's call chain): the round plays once; BUG-0920 then clears the game's
editor-pending flag and the pool is drawn again. `coop.vote` counts it. Off: the meetings go on (0.7.15).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
