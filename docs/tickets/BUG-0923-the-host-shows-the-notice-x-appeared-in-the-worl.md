---
id: BUG-0923
title: The host shows the notice 'X appeared in the world' twice within 0.4 s when a guest joins
status: open
kind: bug
stage: Core
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: host
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: 
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4).
Sheets: `out/lab/20261111-044257/frames/sheets/038_play_space_01_start.jpg` and `out/lab/20261111-044257/frames/sheets/029_play_cell_01_start.jpg`; the same in
`out/lab/20261111-044257/frames/sheets/035_play_creature_01_start.jpg` and `out/lab/20261111-044257/frames/sheets/027_one_creature_01_host_creature.jpg`.
Frames: `out/lab/20261111-044257/frames/play_space/01_start_host.jpg`, `01_start_guest.jpg`.

* Host: the notice list shows "Bob appeared in the world" two times, one under the other (in one_creature a third
  one slides in after the creature switch).
* Guest: one "Alice appeared in the world" (after "receiving world" and "loading the world").
* Host logs: the line is written twice within 0.3 to 0.4 s at the join (play_space 05:18:44.481 and .907, with
  "creations: player 1 ufo = ..." between them; play_creature 05:31:24.382 and .713).

## Root cause
Not analysed. Inferred: the puppet of the guest is made, then made again when the guest's creation record arrives
a moment later, and each making tells the player. Cosmetic; older than 0.7.12 by the look of it (not checked on an
older build).

## Fix
Open (cosmetic).

## Verification
Open. How to test: any two-game start; count "appeared in the world" lines for one player in the host's log within
2 s of the join (expected 1).

## History
- 2026-11-11: opened.
