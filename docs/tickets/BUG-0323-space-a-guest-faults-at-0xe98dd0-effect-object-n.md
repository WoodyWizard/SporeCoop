---
id: BUG-0323
title: Space: a guest faults at 0xE98DD0 (effect object null) when the host's ship appears in an NPC system, lane 8 only
status: fixed
kind: crash
stage: Space
reported: 2026-10-30
source: lab runs 20261030-145308, -150155, -150848 (fix/w13-space) and 20261030-151438 (main 4dbacc5), lane 8
role: guest
versions: main 4dbacc5 and fix/w13-space
fixed_in: 0.7.3.2
branch: fix/w14-space
commits: d903410, a5d117e, 4e8dcd5, 1634c63, 2dbb28a
tests: space_mirror_model
signature: 0xC0000005 at 0x00E98DD0 (labelled cPlanetGfx_Create+0x8F0, really FUN_00e98d90+0x40) data 0x4
docs: docs/re/space.md#space-mirror-model
related: BUG-0111
switch: fleet.model_check
---

## Report
Lab, lane 8 only (GOG 2024), 2026-10-30 from ~15:00: every `coop_space_jump.py` /
`coop_space_seed_profile.py` run on lane 8 (5 of 5, also main 4dbacc5 in lab 20261030-151438 and
the branch with the BUG-0185 fix switched off) had a caught guest fault ~1.4 s after the host's
ship appeared in the NPC system 0046101B the guest had jumped to: `0xC0000005 at 0x00E98DD0 ...
(data 0x00000004)`, eax/esi 0, calls 00E9AE74 00E9290C 00E9294D 00E9B217 00E9950B 00B37D2E
00B3687C 00A44E28 00C37003 00BD790D. The game went on; the lab fails the test on the fault count.
Lane 9 ran the same tests 8 times in the same hour without it; lane 8 ran space_jump 5 times
clean at 13:35-13:50 (lab 20261030-133510) with the same code.

## Root cause
Observed. 0xE98D90 is the ship gfx's SetVisible (`[model+4] |= 1` on its vt+0x2C model without a
null check); the ship had no model. It was the guest's mirror of a host NPC ship: the host sends
each NPC ship's model key (+0x780) and the guest spawned the mirror with it, but an NPC empire's
ships are creations of the host's Sporepedia cache `Pollination.package`, which differs per
profile. With fix/w14-space's log the guest wrote "host ship model 290EC15C/... is not in this
game" 24 ms before the fault. Lane state as the trigger: bisecting lane 8's host profile against
lane 9's (labs 20261030-153604 .. 20261030-162900) moved the fault with the host's
Pollination.package alone (lane 9 profile + lane 8 Pollination: fault; lane 9 profile + lane 8
Planets / caches: none; the whole lane 9 profile on lane 8: 4 clean runs). Inferred: lane 8's host
cache grew with NPC empires' ships in earlier tests (a Civilization -> Space campaign ran there at
14:28-14:43, the first faulting run was at ~14:55). Players hit the same with any two profiles
whose caches differ: it is our bug (the mirror), the lane only exposed it.

## Fix
`squadron.cpp`: before spawning a mirror the guest checks the model key with the resource manager
(`creations::key_known`); an unknown one is replaced with the model of a live ship of that empire
the guest's game made itself (remembered per political id), else the mirror is not made. SpawnUFO
with a null key faulted the same way (tried, lab 20261030-163152), so no default model is used.
Switch `coop.fix fleet.model_check`; `coop.fleet list` prints `unknown_models=`; `coop.fleet
fakemodel 1` (host, tests) sends made-up instances. Not done: sending NPC empires' creations.
Lane 8: the host profile was put back to its original state after the bisection (it reproduces the
bug, which is now handled); the original copy is kept at `<bottles>/SporeLab8/test-bak-bug0323`,
the lane 9 copy used for the swap at `<bottles>/SporeLab8/test-from-lane9`.

## Verification
Test `tools/tests/coop_space_mirror_model.py` (suite `space_mirror_model`): the host sends made-up
model instances and spawns a pirate and an escort; the guest's game must not fault.

* FAIL without the fix: lab 20261030-164009 (lane 9, `SPORE_FIX_OFF=fleet.model_check`): guest
  faults x2 at 0x00E98DD0, the player-report signature.
* PASS with the fix: lab 20261030-164120 (lane 8 and lane 9), no fault. The real case: lab
  20261030-163514 (space_jump on lane 8 with the faulting host profile): no fault, "not in this
  game: the mirror takes a local ship's model".

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-space): root cause found (host NPC ship model missing on the guest; lane 8's host Pollination.package as trigger), fixed behind fleet.model_check.
- 2026-10-30: Space regression on lanes 8/9 with the fix, lab 20261030-170211: 15/15 (space_jump on lane 8 with the faulting host profile included).
- 2026-11-01: released in 0.7.3.2.
