---
id: BUG-0683
title: Creature: a partner's puppet 0.65 m under the ground at the nest while the brain upgrade caption plays after the evolution (monitor rule ground)
status: open
kind: test
stage: Creature
reported: 2026-11-07
source: lab 20261107-141346 campaign_tribe (rel/0.7.10 e7d1734c, lane 5)
role: guest
versions: 0.7.5+
fixed_in:
branch:
commits:
tests:
signature: MONITOR-VIOLATION ground <role>: partner N's puppet 0.5-0.7 m under the ground (Creature) on 2 lines, right after the evolution editor (busy=cinematic,input_lock)
docs:
related: BUG-0504, BUG-0592, BUG-0435
---

## Report
campaign_tribe on rel/0.7.10 (lab 20261107-141346, lane 5) printed RESULT: OK; the lab failed it on the monitor gate
`ground extra2: partner 1's puppet 0.65 m under the ground (Creature) on 2 lines from s 240`. On extra2 (Dave, 4
players) after the evolution editor all four creatures stood at the nest spot (-171.5 -466.9 -83.6, d=0), the
brain upgrade caption played (`busy=cinematic,input_lock`); Bob's puppet read agl 0.24 at s 230, -0.65 at s 240 and
s 250 (drawn_v 0.0), -0.03 at s 260 after the caption (campaign_tribe.extra2.game.log lines 473-494).

## Root cause
* Observed: the same pattern on rel/0.7.5 campaign_tribe (lab rel/0.7.5 20261104-061516: host, partner 1's puppet
  0.55 m under, from s 210, right after the evolution editor at the nest, agl -0.67 / -0.55) and partner puppets
  under the ground on rel/0.7.9 too (creature_carry 20261106-200316 and -231404, 0.74 m / 1.15 m, other spots), so not
  a 0.7.10 regression.
* Inferred (as BUG-0504 for the own creatures): the creatures stacked on one point of the nest mound under the caption
  (paused game time, the mod's frame still runs) are pushed apart and one is pressed under the mound's surface; the
  puppet stands still, so creature.puppet_mound / puppet_ground do not move it until the caption ends. BUG-0504's fix
  (creature.own_ground) covers own creatures only.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
