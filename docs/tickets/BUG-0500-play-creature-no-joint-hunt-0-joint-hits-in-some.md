---
id: BUG-0500
title: play_creature: no joint hunt (+0 joint hits) in some runs, and the host's own creature 0.71 m under the ground (monitor rule ground), with the relay off
status: open
kind: test
stage: Creature
reported: 2026-11-03
source: BUG-0471 A/B, lane 4, 2026-11-03
role: 
versions: 
fixed_in:
branch:
commits:
tests: play_creature
signature:
docs:
related: BUG-0471, BUG-0499
---

## Report
With the effect relay switched off (SPORE_FIX_OFF=creature.effects_relay, fix/w40-effects, lane 4,
out/lab/20261103-124913 in SporeCoop-wt/fix/w40-effects), play_creature#2 and #4 failed `joint hunts
happened (+0 joint hits)`; #2 also failed the lab's monitor gate `ground: host: its own creature 0.71
m under the ground (Creature) on 2 lines from s 290`. main had the joint-hunt failure too
(out/lab/20261103-012624 play_creature#1 in the main checkout). Not the relay's (it was off).

## Root cause
Not analysed. The joint hunt needs both bots to hit the same NPC within the bonus window; with
random choices that does not always happen in 10 minutes (inferred). The host's creature under the
ground for 2 log lines is a separate symptom to check with the monitor's ground rule.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
