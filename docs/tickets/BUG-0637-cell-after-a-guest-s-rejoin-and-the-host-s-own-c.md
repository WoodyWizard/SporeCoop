---
id: BUG-0637
title: Cell: after a guest's rejoin and the host's own-cell change at level 10 the host sees cells that do not belong to the level
status: open
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-03
role: host
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0342, BUG-0349
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-03 (the host, mark at s 630.3): "there are creatures that
shouldn't be in this cell stage". The host's shot is covered by the co-op window.

## Root cause
Observed (the host's log): the guest left at s 594.1 and rejoined at s 595.4; the host saved and shared its world
(3935 KB, done s 616); the guest reappeared at s 621.3. 0.4 s before the mark the host clicked the own-cell arrows in the
co-op window (s 629.0, 629.9), each "own cell is now ... (level rebuilt)" at level 10 (`tiles: check after the frame
between (level 10): pool live 0, list entries 21, dead entries 21`), own cell 2912DEEB -> 2912DEEA -> 2912DEEB; the
host's size 0.48 -> 0.55, its pool small (189-216).
Inferred: the level rebuild after an own-cell change at level 10 (or the rejoin share before it) fills the level with
cells of another level or the players' creations. Not verified. Related: BUG-0342, BUG-0349 (own-cell rebuild, fixed).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: host at Cell level 8-10, press < / > in the co-op window (and a guest rejoin); list the pool's cell types and
     sizes before and after against the level's spawn table. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
