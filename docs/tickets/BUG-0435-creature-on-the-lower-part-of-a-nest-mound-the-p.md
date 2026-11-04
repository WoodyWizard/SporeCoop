---
id: BUG-0435
title: Creature: on the lower part of a nest mound the partner's creature stands 0.4 m in the ground and steps 0.5 m when the partner crosses 0.5 m over the plain terrain
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop (BUG-0414 analysis)
role: guest
versions: 0.7.3.3+
fixed_in: 0.7.5
branch: fix/w41-puppet-look
commits: f7616a06
tests: puppet_mound
signature:
docs: docs/re/creature.md#creature-puppet-look, docs/re/creature.md#creature-out-of-view-ground
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
`creature.puppet_mound` (modes.cpp creature_drive): a grounded partner (no PF_JUMPING) 0.1-0.5 m over GetHeightAt is put
5 cm under its own height instead of 5 cm under the plain terrain; the game sets it onto the mound. Lower (the plain
terrain) and higher as before, so the 0.5 m step is gone too. Off: 0.7.4.

## Verification
puppet_mound (`coop_puppet_walk.py --dir host --goto=-136.4,-487.5,-67.4 --mound=-128.4,-487.5,-67.4 --secs 15`): the
host stands 10 s on the mound's lower part, then walks; on the guest, of the frames with the target over 0.15 m above
GetHeightAt and the puppet on the screen, at most 10 % drawn more than 0.15 m under the host's height.
- FAIL with SPORE_FIX_OFF=creature.puppet_mound: out/lab/20261104-025830 (179 of 303 frames, -0.45 m).
- PASS with the fix: out/lab/20261104-030003 (0 of 1120 frames).

## History
- 2026-11-01: opened (fix/w34-ability-pop).
- 2026-11-04: fixed on fix/w41-puppet-look (creature.puppet_mound).
- 2026-11-04: released in 0.7.5.
