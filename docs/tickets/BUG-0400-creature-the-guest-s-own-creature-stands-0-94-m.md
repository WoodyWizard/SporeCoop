---
id: BUG-0400
title: Creature: the guest's own creature stands 0.94 m under the terrain after its own species replaced the host's (creations swap)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, ground rule), lane 6
role: guest
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w33-unlocks
commits: a876f0d 38c5f65 785b0a4
tests: team, select, creature (warm creature fixture; monitor rule ground)
signature:
docs: docs/06-testing.md#testing-monitor-fields
related: BUG-0367
switch: tools.gopos_ground
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
Not the species swap (ruled out from the log: after the swap at s 13.3 the creature still stood where
the load put it). The run's `team` test (warm, its game log has the same lines) moves the guest next to
the host for the regen check with `coop.npc gopos <host x + 1.5> <host y> <host z>`: the guest's trace
position (-170.7 -466.8 -83.8) is exactly host (-172.07 -466.83 -83.84) + 1.5 m along x. A step along a
world axis is a chord of the planet: there 0.52 m nearer its centre (|p| 504.03 against the host's
504.55), with the slope 0.94 m under the terrain, and the game does not lift a grounded creature placed
under it (docs/re/creature.md#creature-puppet-ground); it stood still, so it stayed there (the 3rd
session of the same games read agl 0.40 at another spot). Reproduced: team with the old gopos, guest
-0.82 m and -0.87 m (`coop.avatar` above=). A test artifact, not a player bug.

## Fix
`tools.gopos_ground` (coop.cpp, `coop.npc gopos`): a point under the terrain is put onto it (radially,
`cPlanetModel` ground radius); `coop.fix tools.gopos_ground off` = as given. The team test now checks
the guest's height over the ground after the move (> -0.3 m). The lab allowance (suite.py ALLOW_ALL) is
removed. Also used by puppet_walk, migration, creature_party, nest_objects, tribe_npc_dash.

## Verification
FAIL: SPORE_FIX_OFF=tools.gopos_ground team --repeat 2, lab 20261101-141844 (lane 6: the guest -0.82 m and
-0.87 m under the ground after the move, 2 of 2).
PASS: team in the smoke run 20261101-143557 (lane 6: the guest 0.31 m over the ground after the move).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w33-unlocks: cause found (the team test's gopos), fixed in the test command (unreleased).
- 2026-11-02: released in 0.7.3.4.
