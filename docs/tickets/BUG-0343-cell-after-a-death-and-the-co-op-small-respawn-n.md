---
id: BUG-0343
title: Cell: after a death and the co-op small respawn next to the partner the host's view is off centre
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player report 2026-10-31 (host mark)
role: host
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md (open question: camera after an avatar teleport)
related: BUG-0041
---

## Report
Host (GOG 2024, Windows, 0.7.3.1), Cell with one guest: "I died in cell stage and now my screen is
off center." The mark came about 50 s after the host's second knockout in a minute.

## Root cause
Observed (host log): twice `own cell state 1` (knocked out, 10 s for the partner), then
`own cell state 3` and "respawned small next to <partner>": the co-op respawn moves the own cell
next to the partner and makes it small (`size=0.41 body=0.50`), instead of the game's own death
and egg. The mark came after the second one; no fault, no rebuild in between.

Inferred: the co-op respawn teleports the avatar (cellsync `stop_at`) and changes its size; the
cell camera keeps its own offset or follows with a lag that the teleport does not reset, so the
view is no longer centred on the cell. docs/re/cell.md lists "camera behaviour after an avatar
teleport" as open.

## Fix
Not done. Plan: after the co-op respawn (and any teleport of the own cell), re-centre the cell
camera on the avatar (find the camera centre the game sets at its own respawn / hatch, RE needed).

## Verification
Test to write: Cell, the host knocked out with the guest away, respawn small next to the guest;
read the camera centre (the visible box G-relative, docs/re/cell.md#cell-new-players-food) against
the own cell's position on the host before and 2 s after the respawn. Must FAIL on main.

## History
- 2026-10-31: opened from the host's report zip (mark and cell trace).
- 2026-11-02: reported again on Discord ('Decentered screen': when the host evolves the cell in the Cell stage, the other player's screen is off centre when it reappears), 0.7.3.4. Target 0.7.4.
