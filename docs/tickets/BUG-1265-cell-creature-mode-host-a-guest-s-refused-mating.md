---
id: BUG-1265
title: Cell, creature.mode host: a guest's refused mating call replays the mating scene and the black fade every ~10 s
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: fix/w53-cellnow
role: guest
versions: 0.7.15.1
fixed_in: unreleased
branch: fix/w53-cellnow
commits: 6b871441 71e78582 27168d0e
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
`cell.refused_mate` (vote.cpp evo_flag_tick, every frame, outside the game's call chain): in creature.mode host a
guest's mate G+0x51D4 is forgotten as soon as its mating call brings it (cellsync::forget_mate; also after a refused
request), so no mating scene, egg laying or fade starts; the guest reads "the host evolves the cell for everybody".
`coop.vote` counts it. Off: the meetings go on (0.7.15). A first version forgot the mate only at the refusal: the loop
ended but the guest's picture stayed black (36 s, lab 20261118-142112): the egg laying's fade is undone only by an
editor's return or the next round.

## Verification
evo_screens_cell_one (`--part cell --one --ready none`, the guest's mating call counted by the game's mating / egg
counters, its screen measured):
* FAIL: out/lab/20261118-142943 (`SPORE_FIX_OFF=cell.refused_mate`, lane 3: 6 matings, 6 egg layings, 5 refused
  requests, 26 s black) and out/lab/20261118-141738 (same), also 20261118-140510 (before the check).
* PASS: out/lab/20261118-142939 (lane 2: 0 matings, 0 egg layings, 0 refusals, the guest told, no black).
* Not BUG-1263: `SPORE_FIX_OFF=cell.editor_now` out/lab/20261118-141258 shows the same loop (black 8 s runs).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellnow): fixed (cell.refused_mate).
