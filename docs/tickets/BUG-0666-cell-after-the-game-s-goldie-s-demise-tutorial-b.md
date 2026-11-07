---
id: BUG-0666
title: Cell: after the game's Goldie's demise tutorial both avatars keep their collect locks (+0x178): no part token can be collected (lab; players unknown)
status: open
kind: bug
stage: Cell
reported: 2026-11-07
source: fix/w46-part-token lab probes 2026-11-07, cell_part_token FAILs 20261106-231404, 20261107-004042
role: both
versions: 0.7.9-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0316, BUG-0633
---

## Report
cell_part_token failed on rel/0.7.9 (out/lab/20261106-231404, 20261107-004042 in the rel/0.7.9 worktree): neither
game collected any part token. Probes (lane 3, 2026-11-07): from the moment the shared food crosses the Goldie window
both avatars have the lock bytes +0x16C, +0x178, +0x17B, +0x17C set (and +0x170 = Goldie's index), animation +0x18C 0,
for good (30+ s, also after own meals and swimming); 0xE7B630 refuses every token while +0x178 is set. Also with the
session left (one game alone with the mod loaded).

## Root cause
Observed (Ghidra): AddFoodPoints 0xE7CE10 calls 0xE79720 once per game when S+0x68 == 0, the food BEFORE the add is
121-150, at most 4 parts are unlocked and the difficulty is not hard: it destroys the cells around the avatar (5 and
15 m), makes two cells (G+0x516C Goldie, and its eater with +0x22C = Goldie), sets S+0x68, stores an event of type
0x24 (15 s) at G+0x5194 / G+0x5198 and plays the cinematic "goldies_demise". The event's handler 0xE794F0 stops the
cinematic, destroys Goldie with its loot (the first part token) and sets the avatar's +0x170 = Goldie. Runtime: the
event fires after its 15 s, the locks stay. A meal crossing food 200 from 190 does not start it (outside the window).
Not known: what clears the locks in a normal game (the cinematic's end? player input?), whether players hit it
(the report's host had collected parts, "3/6"). The test's `coop.pool food 40` steps cross the window.

## Fix
Test only so far: cell_part_token marks the tutorial played on both games (`coop.pool mate nogoldie`, S+0x68 = 1).
The game-side question (does a player's game clear the locks after goldies_demise; does co-op break it) is open.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
