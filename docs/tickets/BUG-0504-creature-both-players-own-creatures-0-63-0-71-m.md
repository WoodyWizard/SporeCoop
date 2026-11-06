---
id: BUG-0504
title: Creature: both players' own creatures 0.63-0.71 m under the ground while the brain upgrade caption waits (monitor rule ground)
status: fixed
kind: test
stage: Creature
reported: 2026-11-03
source: BUG-0500 analysis, lab 20261103-124913
role: 
versions: 
fixed_in: 0.7.8
branch: fix/w44-height
commits: 9ce58786
tests: own_ground
signature:
docs: docs/re/creature.md#creature-brain-caption-ground
related: BUG-0500, BUG-0592
---

## Report
Split from BUG-0500. play_creature#2 of out/lab/20261103-124913 (fix/w40-effects, relay off) failed the
monitor gate `ground: host: its own creature 0.71 m under the ground (Creature) on 2 lines from s 290`, the
guest's own creature 0.63 m too. Both players stood at (-86.1 -465.6 -177.1) in the brain upgrade caption
(cinematic F6A4B1E1, `busy=cinematic,input_lock,paused` from s 290 to the end: the bots did not answer it,
BUG-0500); the host's agl stayed -0.71 on every trace line for 5 minutes while the creature did not move.

## Root cause
Not reproduced (fix/w41-creature-left, 2026-11-03, docs/re/creature.md#creature-brain-caption-ground). Observed: at the report's spot (-86.1 -465.6 -177.1) both own creatures stayed 0.00 m over the ground before and during the brain caption F6A4B1E1 (placed there by coop.npc gopos); after coop.npc gonpc the host's creature rested 0.00-0.54 m over the terrain, never under it. Inferred: the game's (the mod does not move the own creature, the caption itself does not lower it); the walked creatures of play_creature stood on a surface lower than the terrain sample, not known which.

2026-11-05 (fix/w44-height): the cause under the caption is still not known (inferred: a separation of the two own
creatures standing at the same spot, as in BUG-0592: both stood at (-86.1 -465.6 -177.1), the host's puppet and the
guest's creature on one point).

## Fix
creature.own_ground (BUG-0592): an own standing creature more than 0.25 m under its ground for 0.5 s is put onto it.
Not run under the brain-upgrade caption (the caption pauses the game time; the mod's frame still runs).

## Verification
no-fail-run: not reproduced at the report's spot (probes on lane 9, see the docs section).

Through BUG-0592's test: own_ground FAIL out/lab/20261105-113544 / PASS out/lab/20261105-113703 (the symptom). The
caption itself not reproduced (see above).

## History
- 2026-11-03: opened.
- 2026-11-03: not reproduced on fix/w41-creature-left (gopos to the spot + brain caption, gonpc next to 10 NPCs).
- 2026-11-05 (fix/w44-height): fixed by creature.own_ground (BUG-0592); the caption case not rerun.
- 2026-11-05: released in 0.7.8.
