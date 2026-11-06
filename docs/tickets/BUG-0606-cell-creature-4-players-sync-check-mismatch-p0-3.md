---
id: BUG-0606
title: Cell -> Creature, 4 players: sync-check MISMATCH p0 359-740 apart on every guest during the campaign's Cell advance (food 1000 shortcut)
status: open
kind: desync
stage: Cell
reported: 2026-11-05
source: lab fix/w45-crashes 20261105-233139 campaign_ct#1
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests: campaign_ct
signature: sync-check: MISMATCH with the host (Cell ...): p: 1 apart (p0 359-740) right after `coop.pool food 1000` in the Cell advance
docs:
related: BUG-0393, BUG-0377
---

## Report
Found on fix/w45-crashes (0.7.8 + the BUG-0403 hook writer), lab 20261105-233139 campaign_ct#1 (lane 5,
4 players, SPORE_DETECTOR=20): the lab's sync-check gate failed. At 23:35:15 the campaign's Cell advance gave
the host food 1000 (`coop.pool food 1000`, the shortcut); the host's cell grew (size 0.82 -> 2.29 -> 2.68),
its pool went from 1108 to 20 to 389 cells, and every guest's sync-check found the host's player cell p0 apart:
Carol 739.9 (4 objects compared) at 23:35:24, Dave 370.8 and Bob 358.8 (155-161 objects) at 23:35:27 / 31.
The host's trace line showed Dave at d=4482. The advance went on (captions ok, the host played Creature 38 s
later). The same test passed on lane 5 at 23:19 (20261105-231922) and campaign_tribe twice; no other campaign
log of the lab history has a Cell `p0` MISMATCH in the hundreds (searched all `campaign*.game.log`).

## Root cause
Not analysed (outside package X3). Inferred: the sync-check sampled the host's cell during the level change
that the food shortcut forces (the Cell zoom levels rescale positions), like BUG-0393's view jump in Tribe;
not shown whether players see it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not fixed. Seen once: out/lab/20261105-233139 campaign_ct#1.

## History
- 2026-11-05: opened.
