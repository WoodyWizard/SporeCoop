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
branch:
commits:
tests:
signature:
docs:
related: BUG-0500
---

## Report
Split from BUG-0500. play_creature#2 of out/lab/20261103-124913 (fix/w40-effects, relay off) failed the
monitor gate `ground: host: its own creature 0.71 m under the ground (Creature) on 2 lines from s 290`, the
guest's own creature 0.63 m too. Both players stood at (-86.1 -465.6 -177.1) in the brain upgrade caption
(cinematic F6A4B1E1, `busy=cinematic,input_lock,paused` from s 290 to the end: the bots did not answer it,
BUG-0500); the host's agl stayed -0.71 on every trace line for 5 minutes while the creature did not move.

## Root cause
Not analysed. Observed (lane 6 probe, fix/w40-play): at the fixture's nest the own creatures' height over the
ground did not change under the same caption (0.32 / 0.39 m before and during). Inferred: either the game
poses the creature for the caption's camera on that slope, or the terrain height (GetHeightAt, terrain only)
under a creature standing on something there; the mod does not drive the own avatar. With play_creature's
bots answering the caption now (BUG-0500) the rule may no longer see it; to reproduce, AddEvolutionPoints
f:250 with both creatures at (-86 -466 -177) and read `agl=` on the trace lines.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
