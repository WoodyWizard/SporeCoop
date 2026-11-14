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
branch: fix/w49-npcwalk
commits: 1471d307
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
Not a test tolerance: the guest's game really drew the copy standing and jumping (docs/re/creature.md#creature-air-place-lost).

Observed (coop.npctrack rows saved by npc_walk, out/lane4/track/npc_walk_creature_*.json):
- Every failing run is the one walker the flee hit threw on the host (+0x2B0 = 3, +0x137 = 0 for ~2 s, 9.8 m/s
  slowing to ~1 m/s, smooth in every host frame). The guest places its proxy in the air (NF_AIR,
  `drive::air_place`: `game::place` without the teleport mark, not grounded, the host's velocity written; how 5).
- In 70-77 of the 83-91 air frames whose target moved (66 fps) the drawn proxy is exactly where it was a frame
  before (step 0.00-0.01 m) while its target moves 0.1-0.2 m per frame; every ~0.1 s one placing is taken and it
  steps 0.6-1.1 m. Same pattern in 9 older runs back to 20261106 (also on 0.7.13's code: the air placing is
  BUG-0550's, 0.7.7); in 4 of them the walker was off the screen, so the check did not see it. At 32-33 ms frames
  it stood in 16-29 of 44-53 frames with steps under the 0.75 m limit: hidden at a low frame rate, the kind of
  BUG-1092. The passing runs had no throw at all (the hit throws a walker in about half of the runs).
- Not drive.agl_lerp / SnapshotBuffer::sample: the target (the sampled state) moves smoothly; the game does not keep
  the place.
- Not BUG-1093 (puppet_stops: the partner's walk stop, modes.cpp): a different path.

Inferred: as BUG-0361's walked proxy thrown back by a hit, the creature's own update restores its position and takes a
new one only every ~0.1 s; which update, and whether the relayed attack animation (035EE23C / 035EE245, played on both
games) or the frame rate decides it, is not identified (a state-only throw on the guest did not reproduce it at 19-26
fps, see the RE section).

## Fix
`drive::air_place` (driver.h, shared by NPC proxies and Tribe member mirrors) checks whether the game kept last
frame's place: when the body is more than max(0.1 m, 0.5 x speed x frame) from that place moved on by the velocity
written with it, the rest of that air phase is placed with the teleport mark (`game::teleport`), which the game keeps,
as walk_place's keep_lost does on the ground (BUG-0361). A place the game keeps (a lift, a mirror held up, a jump
that flies) is never switched. `coop.fix drive.air_mark off`: always without the mark (0.7.13).
npc_walk also judges a thrown proxy now, on the screen or off it: placed in the air while its target moved, it may
stand in at most 15 % of those frames (judged from 20 such frames on).

## Verification
- FAIL (without the fix; recorded runs of fix/w49-proxy2 = rel/0.7.14 before this branch, judged again by the new
  check `air_follow` from their saved rows): out/lab/20261113-202612 (stood 73 of 84 air frames), 20261113-204146 #2
  (77 of 89), 20261113-204750 (72 of 83); also 20261110-180639 (72/85), 20261108-112359 (75/87). Their own run
  result: FAIL "no jump of a moving proxy (4-6)".
- PASS (with the fix): out/lab/20261113-213112 npc_walk 3/3 (one throw: 19 air frames, under the 20 the check
  needs, at 35 fps; the lab at load ~29 on 12 cores all evening).
- With `SPORE_FIX_OFF=drive.air_mark`: out/lab/20261113-213753 npc_walk 4/4 PASS: the flee hit threw nobody (0-2
  air frames at 22-28 fps), so no live FAIL run of the switch: the throw comes from the host's game in about half
  of the runs at 66 fps and was not seen once at the evening's 20-35 fps (7 runs).
- Not verified live: the fixed proxy in a real 66 fps throw (no throw happened in the runs with the fix).
- Regression: see History.

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-npcwalk): cause from the saved rows (air_place not kept by the game), fix drive.air_mark,
  npc_walk's air check; tries for a deterministic twin (host hits with and without push, a guest-side throw) did not
  reproduce it at the loaded lab's 19-26 fps.
