---
id: BUG-0396
title: tests: cell_coop fails on a warm repeat: it checks the game's revive / respawn counters as absolute numbers
status: open
kind: test
stage: Cell
reported: 2026-11-01
source: lab 20261101-111447 cell_coop#2 (fix/w31-cell-gate); also 20261030-062715, 20261031-202750, 20261101-103417 on other branches
role: 
versions: 
fixed_in:
branch:
commits:
tests: cell_coop
signature:
docs:
related:
---

## Report
`lab.py run cell_coop --repeat N`: every warm run after the first fails "the partner right next to it
revives it (revives 2, hp 5)", "still knocked out after 6 s", "respawn after 10 s", "next to the
partner", "small again" (lab 20261101-111447 cell_coop#2; the same in 20261030-062715,
20261031-202750, 20261101-103417 cell_coop#2).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: `revives=` / `respawns=` of `coop.pool` are counters of the game process (cellsync.cpp
g_revives, g_respawns), kept through the lab's warm reset; the test compared them with 1 and 0. With
respawns already 1 its wait for the respawn ended at once, so the later checks read a cell still
knocked out 59 m away.

## Fix
Test only: the counters are compared with their values at the test's start.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
