---
id: BUG-1094
title: Tests: npc_walk fails now and then on 'no jump of a moving proxy' (a hit NPC's fleeing proxy steps 0.75-1.13 m in single frames at t 45-47 s)
status: open
kind: bug
stage: Creature
reported: 2026-11-13
source: fix/w49-proxy2 regression (lane 4)
role: guest
versions: 0.7.14 (rel/0.7.14 + fix/w49-proxy2)
fixed_in:
branch: fix/w49-npcwalk
commits:
tests: npc_walk
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
Not a test tolerance and not the test's frame rate: the guest's game really draws the copy standing and jumping
(docs/re/creature.md#creature-air-place-lost).

Observed (coop.npctrack rows saved by npc_walk, out/lane4/track/npc_walk_creature_*.json):
- Every failing run is a walker the flee hit threw on the host (+0x2B0 = 3, +0x137 = 0 for ~2 s, 9.8 m/s slowing to
  ~1 m/s, smooth in every host frame). The guest places its proxy in the air every frame (NF_AIR,
  `drive::air_place`: `game::place`, not grounded, the host's velocity; how 5).
- In 66-79 of the 77-91 air frames whose target moved (52-67 fps) the drawn proxy is where it was a frame before while
  its target moves 0.1-0.2 m per frame; every ~0.1 s one placing is taken and it steps 0.6-1.1 m. In the standing
  frames even the position right after the mod's placing (`p`) equals the drawn one. The same in 9 older runs back
  to 20261106 (0.7.13's code; the air placing is BUG-0550's, 0.7.7); in 4 of them the walker was off the screen. At
  32-33 ms frames it stood in 16-29 of 44-53 frames with steps under the 0.75 m limit: hidden at a low frame rate,
  the kind of BUG-1092. The passing runs had no throw (the hit throws a walker in a third to a half of the runs).
- Not SnapshotBuffer::sample / drive.agl_lerp: the target moves smoothly. Not the animation relay (npc.anims off:
  still 69-74 of 81-88 standing). Not BUG-1093 (puppet_stops: the partner's walk stop in modes.cpp).
- Putting the hold back after the creature update and the physics update does not help: the update moves it back off
  its hold in every frame by a constant per ~0.1 s window (0.94, 1.89, 2.72 m), the physics update leaves it there,
  and the next frame is drawn standing again.

Inferred: a game update after the physics update (or one that keeps its own copy of the position, refreshed every
~0.1 s) owns a thrown, ungrounded creature's position; not identified.

## Fix
Not fixed on fix/w49-npcwalk. Tried and dropped (each FAILED with the new check, run ids in the RE section): the rest
of the air phase placed with the teleport mark; also put back after cCreatureAnimal::Update and PhysicsManager_Update
(`drive.air_keep`); no velocity written while kept. Kept on the branch: npc_walk's air check (it fails in every run
with a throw, on or off the screen, so the bug is no longer hidden by the camera or the frame rate).

## Verification
- FAIL (base code, live, the new check): out/lab/20261113-221929 npc_walk #2 (stood 148 of 174), #3 (128 of 166);
  recorded runs judged again from their rows: 20261113-202612 (73/84), -204146 #2 (77/89), -204750 (72/83).
- PASS: none (not fixed). Runs with the tried changes: 20261113-222533, -223254, -224612, -225641, -230443 (all FAIL
  in the runs with a throw).
- no-fail-run: not applicable (open).

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-npcwalk): cause narrowed from the saved rows and live runs (the game does not keep an air
  placing of a thrown proxy); three fixes tried and dropped; npc_walk's air check added; left open for a dedicated
  RE pass (which update owns the position after the physics update).
- 2026-11-13 (fix/w49-npcwalk): a deterministic twin tried and dropped: host hits with and without a push and a
  guest-side state-only throw did not reproduce it (the host's hit then did not throw; the guest-only throw was
  kept at 19-26 fps).
