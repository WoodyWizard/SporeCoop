---
id: BUG-0434
title: Creature: the partner's creature standing on a nest mound is drawn up to 1 m low for one frame when it comes into the observer's view
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop (BUG-0414 analysis)
role: guest
versions: 0.7.3.3+
fixed_in: unreleased
branch: fix/w45-remote-bodies-2
commits: 13376c7e, 37703e65
tests: puppet_mound_view (tools/tests/coop_mound_view.py), puppet_fight_player
signature:
docs: docs/re/creature.md#creature-out-of-view-ground, docs/re/creature.md#creature-culled-ground-fix
related: BUG-0414, BUG-0435
---

## Report
Found while analysing BUG-0414 (lane 4, guest at 30 fps, netem `player`): the host's creature stands on the
fixture's nest mound (1.03 m over `GetHeightAt`); the guest turns its camera. Every time the puppet comes back
into the guest's view it is drawn on the plain terrain (1 m low) for one frame, then on the mound. 26-28 such
frames in 70 s of camera turns (`lod_exp` experiment, out/lane4/track/lod_exp_20261101-163454.json).

## Root cause
Observed: the game puts a creature outside the view on the plain terrain (GetHeightAt) and lifts it onto the mound one update after it is in view again (docs/re/creature.md#creature-out-of-view-ground). New (2026-11-06): not the physics' simple path (+0xF90 = 1 and LOD +0xEAC = 2 in every off-screen low frame, out/lab/20261106-042950); static RE of PhysicsManager_Update / 0xB4A820 found no view flag; inferred: the mound's collision is only loaded while the nest is drawn. Observed: a lift right after the physics update holds until the frame is drawn (the grounding is in or before that update), where writes at the mod's frame did not.

## Fix
`creature.culled_ground` (game.cpp after_physics, called from gamefixes.cpp's PhysicsManager_Update hook after the game's update): a puppet whose target this frame stands > 0.3 m over GetHeightAt and that the game put within 0.1 m of it (within 2.5 m of the target) is lifted back to its target's height over the ground (`mound_lifts=` in `coop.track air`). Off: 0.7.8.

## Verification
puppet_mound_view (the host on the fixture's nest mound, the guest 10 m away turning its camera; the guest's own drawn puppet height in every frame):
- FAIL: out/lab/20261106-050722 (SPORE_FIX_OFF=creature.culled_ground: 200 frames on the screen drawn on the plain terrain, one per entry into the view).
- PASS: out/lab/20261106-045947 (2 runs: 0 frames low on or off the screen, 200 entries, 2754 lifts).

## History
- 2026-11-01: opened (fix/w34-ability-pop).
- 2026-11-04: fix/w41-puppet-look: still open.
- 2026-11-05 (fix/w44-height): not part of the height fix: the culled puppet on a mound is grounded by the game's update outside the view; not looked at again.
- 2026-11-06 (fix/w45-remote-bodies, M1): a different mechanism from the class's trail (BUG-0425): the game grounds a
  culled creature on the plain terrain outside the view (docs/re/creature.md#creature-out-of-view-ground), one frame low
  when it comes back into view; no placing or timing of ours runs between that update and the drawing, so the lead
  (drive.lead) cannot change it. Not touched, stays open.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): F90 / LOD probe disproved the simple-path theory; fixed by a lift after the physics update (creature.culled_ground); FAIL 20261106-050722, PASS 20261106-045947.
