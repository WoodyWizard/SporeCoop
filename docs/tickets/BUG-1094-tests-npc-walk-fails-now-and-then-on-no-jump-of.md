---
id: BUG-1094
title: Tests: npc_walk fails now and then on 'no jump of a moving proxy' (a hit NPC's fleeing proxy steps 0.75-1.13 m in single frames at t 45-47 s)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-13
source: fix/w49-proxy2 regression (lane 4)
role: guest
versions: 0.7.14 (rel/0.7.14 + fix/w49-proxy2)
fixed_in: unreleased
branch: fix/w49-throw
commits: d8a778b3, 97715d0c, 0dbe6cab, 4dd00036, 2d0c47bf
tests: npc_walk, net_tests test_agl_extrap
signature: npc_walk `FAIL no jump of a moving proxy (4-6)`, all of one hit walker, 0.75-1.13 m steps of 12-16 ms frames at t 45.3-45.6 and 47.0-47.3 s
docs: docs/known-issues.md#issue-npc-walk-hit-jumps, docs/re/creature.md#creature-air-place-lost, docs/06-testing.md#testing-npc-track
related: BUG-0409, BUG-0440, BUG-0445
---

## Report
npc_walk (Creature, lane 4) failed 3 of 6 runs on fix/w49-proxy2 on its own check "no jump of a moving proxy": one
of the two walkers the host's avatar hits (`coop.npc hosthit`) steps 0.75-1.13 m in single frames on the guest's screen,
always in two bursts of 3 about 0.11 s apart at t 45.3-45.6 s and 47.0-47.3 s (the flee after the hit):
out/lab/20261113-202612 (4 jumps, #18), -204146 #2 (6, #35), -204750 (5, #20). Passed: -194347, -204146 #1, -204521.

## Root cause
Not the game: the mod's own target height for a body in the air (docs/re/creature.md#creature-air-place-lost).

Observed (coop.npctrack rows, out/lane4/track/npc_walk_creature_20261113-222314.json of lab 20261113-221929 #2, the
offsets split into the radial and the sideways part):
- The throw is straight up (sideways 0.00-0.01 m between drawn, placed and target). In every standing frame the
  position right after the mod's placing is 0.10-0.15 m under the drawn one (the written velocity x the frame), and
  the target rises away from it by one frame's rise per frame (0.16, 0.39 ... 0.91 m), back to 0.0-0.2 m at each new
  state. So the placing went to one height for a whole state period; the game's update moved the proxy up by its
  velocity (drawn = placed + v x dt) and the next placing put it back. What fix/w49-npcwalk read as "the game puts it
  back" was the mod's own placing.
- `cModelObject_SetPosition` 0xC88800 (under the creature's Teleport 0xC0C040) writes unconditionally (Ghidra): no
  placing is refused by the game.

From the code: `drive::air_place` places at `drive::at_sender_agl(s.pos, s.agl)`: this game's ground + the sample's
agl (the host's height over its terrain). The NPC proxies' SnapshotBuffer runs the lead (`drive.lead`): the newest
state moved on by its velocity, with `out = n.s` keeping that state's agl; so did the interpolation's extrapolation
and the smoothing offset. Only the blend between two states moved the agl (BUG-1092). A body moving up or down past
its newest state was pinned at that state's height until the next state (every 0.1 s): the 0.1 s period and the
0.6-1.1 m steps (6-9.8 m/s x 0.1 s).

Two smaller steps found with the fix on:
- At the throw's start a walked proxy trailing its target 0.8-0.9 m, and the lead's target leaping 1-1.5 m at the
  first air state, were placed on at once: 0.94-1.07 m in a 15-19 ms frame (labs 20261113-235647 #2, 20261114-000525
  #3).
- At the landing the host's own game puts the thrown NPC onto its ground: 2.79 m in one host frame (host rows, lab
  20261114-001415 #2, id 38); the guest showed the same step, which npc_walk counted as the proxy's jump.

Inferred: the guest-only test throw of fix/w49-npcwalk was kept because its states had no lead moving them on.

## Fix
- `coop.fix drive.agl_extrap` (net/interp.h `agl_follow`): wherever a sample moves a state's position (the lead, the
  extrapolation, the smoothing), its agl goes with the position's radial move; never under the sender's ground (a fall
  extrapolated on a slow host went 2.7 m under it at the landing, lab 20261113-234048 #2).
- `coop.fix npc.air_catch` (npc.cpp): the drawn proxy moves toward its air target at no more than 1.2x its speed +
  8 m/s, the game's own move by the written velocity allowed for; over 3 m off it is placed on the target.
- npc_walk: a jump the host's own NPC made too (host rows within 0.5 s, at least half the step) is reported ("the
  host's NPC jumped too"), not judged.

## Verification
- Unit twin: net_tests test_agl_extrap (a body thrown up at 9.8 m/s, the proxy's buffer, 60 fps): stood 0 of 36,
  largest step 0.22 m; `SPORE_FIX_OFF=drive.agl_extrap`: stood 30 of 36, steps 0.98 m (FAIL). `make test` passes.
- FAIL (live, `SPORE_LAB_FIXOFF=npc_walk=drive.agl_extrap`): out/lab/20261114-002222 #2 (final code, 58 fps: stood
  41 of 82), 20261113-234018 #3 (stood 39 of 53, jumps 0.84 and 1.3 m); before the fix 20261113-221929 #2, #3.
- PASS (live, final code 4dd00036 + the test rule): out/lab/20261114-002302 #1 (stood 3 of 82), #2 (4 of 168, two
  thrown), #3; 20261114-001459 #1 (2 of 89, 59 fps), #2, #3; 20261114-001415 #1, #3 (0 of 62), #4, and #2 (0 of 61,
  its one 2.9 m step is the host's own landing, judged again with the test rule: no jump).
- Regression (final code): out/lab/20261114-001429 npc_air, puppet_air, npc_walk_tribe, creature_npc_dash,
  tribe_proxy_held: all RESULT OK (also 20261113-235717, 20261114-000615).

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-npcwalk): cause narrowed from the saved rows and live runs (the game does not keep an air
  placing of a thrown proxy); three fixes tried and dropped; npc_walk's air check added; left open for a dedicated
  RE pass (which update owns the position after the physics update).
- 2026-11-13 (fix/w49-npcwalk): a deterministic twin tried and dropped: host hits with and without a push and a
  guest-side state-only throw did not reproduce it (the host's hit then did not throw; the guest-only throw was
  kept at 19-26 fps).
- 2026-11-14 (fix/w49-throw): cause found in the saved rows (the radial split): the lead kept the newest state's agl;
  fixed with `drive.agl_extrap` (clamped at the ground), `npc.air_catch` for the throw's start; npc_walk leaves out
  the host's own landing step.
