---
id: BUG-0435
title: Creature: on the lower part of a nest mound the partner's creature stands 0.4 m in the ground and steps 0.5 m when the partner crosses 0.5 m over the plain terrain
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop (BUG-0414 analysis)
role: guest
versions: 0.7.3.3+
fixed_in:
branch: fix/w34-ability-pop
commits:
tests:
signature:
docs: docs/re/creature.md#creature-out-of-view-ground
related: BUG-0366, BUG-0414, BUG-0434
---

## Report
Found while analysing BUG-0414 (lane 4): with the host standing on the lower part of the fixture's nest mound
(0.39-0.44 m over `GetHeightAt`, its own creature grounded there), the guest drew the host's creature at -0.05 m,
i.e. 0.44 m in the ground, for as long as it stood (6+ s, out/lane4/track/raised_exp_20261101-161637.json, 0-16 s).
Walking over the mound's edge the puppet stepped 0.4-0.5 m up or down when the host crossed 0.5 m (17.2 s, 29.0 s).

## Root cause
Observed: `creature.puppet_ground` (BUG-0366, drive::on_own_ground) puts a target from 3 m under to kOverGround
(0.5 m) over `GetHeightAt` 5 cm into it. `GetHeightAt` is the plain terrain without the mound, so a partner on the
mound's lower part is put under the mound's surface, and the game does not lift a creature put under its ground.
Above 0.5 m the target is left where it is (on the mound): the 0.5 m line is a step.

## Fix
Not fixed (not part of BUG-0414's branch). Idea: take this game's ground where the puppet stands (its drawn height
when it is grounded and in view) instead of `GetHeightAt` near raised ground.

## Verification
Reproduce: the host at (-128.4, -487.5, -67.4) lifted 0.5 m (`coop.npc gopos`), the guest watching; the puppet's
`coop.track` hd -0.05 against ht 0.40.

## History
- 2026-11-01: opened (fix/w34-ability-pop).
