---
id: BUG-0498
title: Creature: rocks appear in one game's object list only, dozens at once 100-160 m from the walking player (detector unmatched:rock)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, fix/w40-world-objects lane 9 out/lab/20261103-131403
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-cell-evo
commits: eca11f6d, e136676f
tests: world_lod (coop_world_lod.py)
signature:
docs: docs/re/creature.md#creature-plants-rocks-lod, docs/06-testing.md#testing-world-lod
related: BUG-0473
---

## Report
Detector `Creature:unmatched:rock` in 62% of the comparisons of out/lab/20261103-131403 (1 rock on the host, 6-7 on the guest near it), none in the three runs before. Live (lane 9): the guest's list had 1 rock (serial 2554, as the host), 29 after its avatar walked 20-30 m (serials 5323.., 100-160 m from it), 1 again 40 s later; the host's stayed 1. Inferred: rocks made by each game near its own camera (like hanging fruit), so a detector artefact; not checked in Ghidra. Not caused by worldobj.cpp (no rock kind). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (coop_world_lod.py, out/lab/20261103-161705): the walking guest's game adds 28 rocks 30-75 m ahead of it
and keeps them; the host's game, whose player stood 1 m from the guest at the start, never has them. Made by each
game around its own camera: not a desync.

## Fix
The detector leaves `rock` out (worlddiff.IGNORE), see BUG-0473.

## Verification
world_lod PASS out/lab/20261103-161705.
no-fail-run: a detector compare rule (worlddiff.IGNORE), not game code.

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-cell-evo: per-camera objects, left out of the detector.
- 2026-11-03: released in 0.7.4.
