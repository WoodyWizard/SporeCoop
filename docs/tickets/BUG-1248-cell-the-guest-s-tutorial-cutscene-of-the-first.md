---
id: BUG-1248
title: Cell: the guest's tutorial cutscene of the first part pickup frames empty water
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: 0.7.15.2
branch: fix/w53-cellfx
commits: 3f0b8d42
tests: cell_goldie_view
signature:
docs: docs/re/cell.md#cell-goldie-cutscene
related: BUG-0666, BUG-0316
---

## Report
A players' video: the tutorial vignette of the first cell part fires; the guest gets the cutscene bars but no cell is in the frame (empty water). Suggested cause: the game's local tutorial cutscene aims at an object or a place the guest's game does not have in focus (the mirror of the host's pool not at the trigger).

## Root cause
Observed (Ghidra + runtime, docs/re/cell.md#cell-goldie-cutscene). The tutorial is the game's own and runs in each game
by itself when the shared food crosses 120-150: 0xE79720 makes two local cells next to the own avatar (Goldie and the
cell that hunts it), aims the cell camera at Goldie (cCellUI +0xE0) and plays the cinematic goldies_demise. On a guest
these two cells do not exist on the host, so guest_state's purge of "stray local cells" destroyed them with the next
host state (0.1 s). The guest's camera stayed on a dead index; ~2 s later the event step 0xE5F160 aimed it at G+0x516C,
the first part-token cell the visibility step saw, which is a mirror of some host cell when one is near the guest, and
none when the guest is away from the host: the letterbox over empty water.
The "Suggested cause" (a host-owned object) was close: the tutorial is local, its cells were removed by the mod.

## Fix
`cell.goldie_keep` (cellsync_guest.cpp `tutorial_cells`): while the tutorial runs (G+0x518C), the stray purge keeps its
event's two cells (event type 0x24 at G+0x5198 / G+0x5194: +0x28 Goldie, +0x2C the hunter; else the camera's cell and
the cell chasing it seen at the start). After it the game clears G+0x518C and they go as before. Off: purged (old).

## Verification
`tools/tests/coop_cell_goldie_view.py` (suite cell_goldie_view): the guest 60 m from the host, the host's food crosses
the window in steps of 10; the guest's `coop.pool goldie` sampled for 6 s of the cutscene, key frames of both games.
* FAIL (`SPORE_FIX_OFF=cell.goldie_keep`): out/lab/20261118-124015 (Goldie / camera cell / hunter alive in 0 of 17
  samples; the guest's key frame: its own cell, no Goldie, no hunter). Earlier versions of the test: -122658, -123002.
* PASS: out/lab/20261118-123823 (19 of 19; the guest's key frame shows Goldie and its hunter as the host's does), out/lab/20261118-125159 (19 of 19), out/lab/20261118-133339.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellfx): reproduced (the guest away from the host) and fixed (`cell.goldie_keep`).
- 2026-11-18: released in 0.7.15.2.
