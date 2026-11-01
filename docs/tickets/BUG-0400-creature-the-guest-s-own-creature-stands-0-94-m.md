---
id: BUG-0400
title: Creature: the guest's own creature stands 0.94 m under the terrain after its own species replaced the host's (creations swap)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, ground rule), lane 6
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests: team, select, creature (warm creature fixture; monitor rule ground)
signature:
docs: docs/06-testing.md#testing-monitor-fields
related: BUG-0367
---

## Report
The monitor's `ground` rule (own trace line `agl=`): in the smoke run out/lab/20261101-120947-feat-w32-monitor-diag
(lane 6, creature fixture, a warm test after the guest's rejoin) the guest's avatar was the host's species
065CBE70 at (-171.6 -467.5 -84.0), agl 0.00; at s 13.3 `creations: own avatar is now 290225CD` (the guest's
own creature replaces it); from then on its own line says pos (-170.7 -466.8 -83.8), **agl -0.94** (s 20,
s 30 and again after the next rejoin, standing still): ~1 m nearer the planet's centre than the avatar it
replaced. Elsewhere the same species reads 0.00-0.41 m. Lane log
SPORE-lab/lane6/.../previous/sporekit.12-20261101-122313.log. No test failed on it (the 2 lines in a row
fell across two tests).

## Root cause
Not analysed. Likely the swap (creations.cpp, own species) places the new creature at a height that
is under the terrain and the game does not lift a grounded creature placed under it
(docs/re/creature.md#creature-puppet-ground). Whether the creature is drawn sunk into the ground is not
checked on a screenshot.

## Fix
Open. Allowed in the lab meanwhile (suite.py ALLOW_ALL, monitor kind `ground`, the guest's own creature).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
