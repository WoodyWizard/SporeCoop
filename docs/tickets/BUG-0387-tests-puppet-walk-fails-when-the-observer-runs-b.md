---
id: BUG-0387
title: tests: puppet_walk fails when the observer runs below ~40 fps (0.6-1.4x in 76-82 % of the windows)
status: duplicate
kind: test
stage: Creature
reported: 2026-11-01
source: found on fix/w30-npc-walk (regression runs)
role: both
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk
commits:
tests: puppet_walk
signature:
docs:
related: BUG-0394, BUG-0360, BUG-0363
---

## Report
`puppet_walk` (BUG-0360) failed in 3 of 5 runs on fix/w30-npc-walk while other lanes ran games (the partner
puppet's driving, creature_drive, is not changed on that branch): the observer at 25-37 fps had the puppet at
0.6-1.4x the walker's speed in 76-82 % of the 0.1 s windows (limit 85 %, standing 0-3 %), at 42-55 fps
93-99 %. out/lab/20261101-094111 (guest 25 fps: 79 %), 20261101-095149 (37 fps: 82 %), 20261101-095826 #1
(host 27 fps: 76 %; #2 at 54-55 fps: 98-99 %).

## Root cause
Not analysed. Inferred: at low frame rates the walked puppet is one locomotion step (speed x frame time, 0.18 m
at 27 fps) off its path between two placings, which moves the 0.1 s windows' speed ratio (as for the NPC
proxies at 15 fps, docs/re/creature.md#creature-npc-walk). Whether players see steps there is not known.

## Fix
None.

## Verification
no-fail-run: not fixed.

## History
- 2026-11-01: duplicate of BUG-0394 (the same failures; causes and fixes there).
- 2026-11-01: opened.
