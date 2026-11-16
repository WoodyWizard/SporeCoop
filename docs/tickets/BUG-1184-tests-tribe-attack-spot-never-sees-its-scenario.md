---
id: BUG-1184
title: Tests: tribe_attack_spot never sees its scenario since tribe.form_pace (coop.tribe form held at food 15, no new tribe to attack)
status: open
kind: test
stage: Tribe
reported: 2026-11-15
source: feat/w51-tribe lab 20261115-182010, rel/0.7.15 9362bcdd 20261115-212642
role: host
versions: rel/0.7.15
fixed_in:
branch: feat/w51-tribe
commits:
tests: tribe_attack_spot
signature:
docs: docs/re/tribe.md#tribe-attack-spot
related: BUG-0582, BUG-0943, BUG-0571
---

## Report
tribe_attack_spot (BUG-0582's test) fails in the final Tribe run of feat/w51-tribe (20261115-182010) and its rerun
(20261115-211527), and the same on rel/0.7.15 9362bcdd (20261115-212642, the worktree tag/w51tribe-c8base): "FAIL an
attacker met the missing spot and ended its attack (the scenario happened: (0, 0))"; no fault, the host draws on. It had
not run since 0.7.7 (fix/w43-crashes, rel/0.7.7 passed with (2, 0)).

## Root cause
Observed: the host's line "tribes 2 -> 2 (next species ... baked) ... (pace: food 15, threshold 100 x 2 players: held)":
`coop.tribe form` formed no village, because tribe.form_pace (BUG-0571) holds a new tribe until the party's food is over
the threshold times the players. The test's creature attack then had no new village's attackers to meet the cut spot.
The same cause as BUG-0943 in tribe_bounce_world and realign_tribe, whose fix gave the host food first; this test was
not changed then. A test problem, no mod bug.

## Fix
coop_tribe_attack_spot.py gives the host 300 food (`coop.res add food 300`) before `coop.tribe form`, as BUG-0943;
`ATTACK_SPOT_OLD_FOOD=1`: no food first (the old setup).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
