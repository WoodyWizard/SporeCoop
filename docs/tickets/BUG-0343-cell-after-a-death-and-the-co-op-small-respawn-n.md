---
id: BUG-0343
title: Cell: after a death and the co-op small respawn next to the partner the host's view is off centre
status: fixed
kind: bug
stage: Cell
reported: 2026-10-31
source: player report 2026-10-31 (host mark)
role: host
versions: 0.7.3.1
fixed_in: 0.7.5
branch: fix/w41-cell-left
commits: 9b03a8ce
tests: cell_camera (tools/tests/coop_cell_camera.py)
signature:
docs: docs/re/cell.md#cell-camera
related: BUG-0041
---

## Report
Host (GOG 2024, Windows, 0.7.3.1), Cell with one guest: "I died in cell stage and now my screen is
off center." The mark came about 50 s after the host's second knockout in a minute.

## Root cause
Observed (Ghidra and lane 4): the cell camera (cCellUI, update 0xE600A0) moves its target +0x54 towards the avatar
only at 1.5 x the avatar's speed and clamps the drawn point +0x48 into a 7 x 5 m box around the avatar. The mod's
jumps of the own cell (cellsync stop_at: the co-op respawn next to the partner, the guest's move next to the host
after a load / an own editor / an own-cell rebuild) left the target behind: the drawn point stayed exactly 7.0 m
from the cell, 3 s later still (both games, lab 20261104-020658), until the player swam long enough. The game's
own jumps (0xE611B0, the hatch sequence 0xE794F0, RebuildLevel) set +0x48, +0x54 and +0x60 to the new place.
Inferred: the reports "after the editor" are the same jump (join after load); not reproduced with an editor.

## Fix
`cell.camera_snap` (cellsync.cpp camera_snap): a stop_at of the own cell more than 2 m from where it is sets the
three camera points to the new place, as the game's own jumps do. Off: 0.7.4. `coop.pool cam` prints the camera.
Lineages had ported the same camera reset selectively (2026-11-07, fix/lineages-player-session,
75b3901b); the 0.7.8 merge keeps upstream's implementation.

## Verification
FAIL: SPORE_FIX_OFF=cell.camera_snap cell_camera, lab 20261104-020658 (7.0 m after the guest's move and after the
host's respawn).
PASS: cell_camera, lab 20261104-020550 (0.08 m / 0.22 m).
Lineages port (2026-11-07, tools/tests/lineages_cell_report.py --camera-only):
out/player-report-20261107-223741/native-camera.log records the negative-control FAIL (fix off, camera
offset 7.0) and PASS twice (0.06 and 0.07). Both runs used native lethal hits, the ten-second knockout and
actual respawn, not a direct camera write from the test. No caught native faults.

## History
- 2026-11-07: reported again in Lineages 27. The fork still lacks upstream's
  camera-snap change. Investigating a selective port on fix/lineages-player-session
  with isolated Cell profiles; user report also tracked by BUG-9096.
- 2026-10-31: opened from the host's report zip (mark and cell trace).
- 2026-11-02: reported again on Discord ('Decentered screen': when the host evolves the cell in the Cell stage, the other player's screen is off centre when it reappears), 0.7.3.4. Target 0.7.4.
- 2026-11-03: reported again (0.7.3.4 forum post): 'decentered screen in the Cell stage (after the editor)'; the trigger the player names is the editor, not a death: maybe a second path. No log evidence in the reports.
- 2026-11-04: fix/w41-cell-left: the camera reset after every jump of the own cell (cell.camera_snap); fixed (unreleased).
- 2026-11-04: released in 0.7.5.
