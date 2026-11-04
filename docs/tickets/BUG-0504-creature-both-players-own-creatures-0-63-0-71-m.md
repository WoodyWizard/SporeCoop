---
id: BUG-0504
title: Creature: both players' own creatures 0.63-0.71 m under the ground while the brain upgrade caption waits (monitor rule ground)
status: open
kind: test
stage: Creature
reported: 2026-11-03
source: BUG-0500 analysis, lab 20261103-124913
role: 
versions: 
fixed_in:
branch: fix/w41-creature-left
commits:
tests:
signature:
docs: docs/re/creature.md#creature-brain-caption-ground
related: BUG-0500
---

## Report
Split from BUG-0500. play_creature#2 of out/lab/20261103-124913 (fix/w40-effects, relay off) failed the
monitor gate `ground: host: its own creature 0.71 m under the ground (Creature) on 2 lines from s 290`, the
guest's own creature 0.63 m too. Both players stood at (-86.1 -465.6 -177.1) in the brain upgrade caption
(cinematic F6A4B1E1, `busy=cinematic,input_lock,paused` from s 290 to the end: the bots did not answer it,
BUG-0500); the host's agl stayed -0.71 on every trace line for 5 minutes while the creature did not move.

## Root cause
Not reproduced (fix/w41-creature-left, 2026-11-03, docs/re/creature.md#creature-brain-caption-ground). Observed: at the report's spot (-86.1 -465.6 -177.1) both own creatures stayed 0.00 m over the ground before and during the brain caption F6A4B1E1 (placed there by coop.npc gopos); after coop.npc gonpc the host's creature rested 0.00-0.54 m over the terrain, never under it. Inferred: the game's (the mod does not move the own creature, the caption itself does not lower it); the walked creatures of play_creature stood on a surface lower than the terrain sample, not known which.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced at the report's spot (probes on lane 9, see the docs section).

## History
- 2026-11-03: opened.
- 2026-11-03: not reproduced on fix/w41-creature-left (gopos to the spot + brain caption, gonpc next to 10 NPCs).
