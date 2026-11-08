---
id: BUG-0686
title: Creature: the host faults at 0xE99524 (the ship gfx factory, null model) when the Drama Manager's UFO visit brings a ship without a model (campaign_tribe "stopped in creature")
status: fixed
kind: crash
stage: creature
reported: 2026-11-07
source: lab
role: host
versions: 0.7.10
fixed_in: unreleased
branch: fix/w46-advance-reg
commits: 028c5863 fce291d5
tests: creature_ufo_nomodel
signature: 0xC0000005 at 0x00E99524 SporeCoop::UfoGfx_SetVisible+0x794 (= UfoGfx_Create+0x54) data=0x4 (host, main); calls 00B37D2E 00B3687C 00C37003 00BD790D 00AC1F2B 0088568C; later 0x00E9A908 data 0x18
docs: docs/re/space.md#space-ufo-gfx-null, docs/06-testing.md#testing-creature-ufo-nomodel
related: BUG-0546, BUG-0687
---

## Report
The 0.7.10 final check's campaign_tribe (4 players, Creature -> Tribe) failed "the campaign stopped in creature
(wanted tribe)" on lab lane 2: lab 20261107-171750 (env1 retry) and 20261107-175737-2 #2, both rel/0.7.10 4c1d891f.
The host (Alice) faulted once at 0x00E99524 at creature/advance, a second after the evolution vote opened, right
after "drama: ufo event starts", "ship model 290F9F22 (290FD328) ... is not in this game: none known, kept" and two
"SetVisible skipped" lines; the caught fault left the game broken (in -175737-2: 0xE9A908 86 s later, then faults in
the galaxy menu and the load: GlobalUI_FindWindowByID, GGE_Exit, and the host stayed in the galaxy menu).
Taken at first for a 0.7.10 regression (v0.7.9 passed 2/2 on lane 2).

## Root cause
Not a regression: the game's own null check missing, reached through the lab lanes' Sporepedia caches.
* ✔ (Ghidra) 0xE99524 is in the ship gfx factory 0xE994D0 (`SporeCoop::UfoGfx_Create`): after InitFromUfo it writes
  GetModel()+4 |= 0x20 outside mode 0x1654C10 with no null check. The listener 0xE9A7A0 writes GetModel()+0x18 on
  message 0x604C6F4 (the second fault 0xE9A908).
* ✔ The model world makes a model for a ship whose Pollination record is a summary only when GraphicsCache.package
  holds its bake. Lane 2's host cache: 123 UFO summaries, no complete UFO, 20 baked; the crash keys 290FD328 and
  290F9F22 are not baked, 290F6D76 (no fault in two other campaign runs) is. Lane 3: 92 / 0 / 2; lanes 4 and 5 hold
  32 / 34 complete UFOs (the final check's campaign_tribe on lane 5 passed). BUG-0546's game.ufo_model cannot help: a
  Creature game knows no ship model ("none known, kept").
* ✔ The same fault on v0.7.9 (lane 3, creature_ufo_nomodel, the first visit): the build does not matter.
* ✔ campaign_tribe A/B (SPORE_DETECTOR=20): v0.7.9 lane 3: 4/4 OK (one visit, a baked ship); e7d1734c lane 4: 4/4
  OK (no visit); 4c1d891f lane 2: 4/4 OK (no visit); before, 4c1d891f failed 3 of 7 runs (lanes 2, 3). The visit comes in about 1 run of 3 (random), at the stage's end.
* ~ Inferred: the visit picks a complete UFO when the cache has one (no "not in this game" line on lanes 4/5).

## Fix
gamefixes, `game.ufo_gfx` (`coop.fix game.ufo_gfx off` = the game's code): the factory is hooked and done the game's
way (GeneralAllocator::new_ 0xD8 "Simulator/cUFOGfx", UfoGfx_ctor 0xE993A0, vt 0x24 InitFromUfo) with the flag
written only on a model; the listener 0xE9A7A0 returns for 0x604C6F4 on a gfx without a model. Such a ship is not
drawn. Counters in `coop.status`. Test command `coop.ufoforget` (game.ufo_model forgets the last known ship).

## Verification
creature_ufo_nomodel (lane 5, the BUG-0546 cache): FAIL with `SPORE_FIX_OFF=game.ufo_gfx` out/lab/20261107-185421
and out/lab/20261107-190843 (2/2: 0xE99524 on the first visit, the campaign's call stack); PASS out/lab/20261107-185603
#1 and out/lab/20261107-191143 (3/3: "ship gfx made without a model 1", no fault, 40 s on). v0.7.9 (the test file
copied into a v0.7.9 worktree, lane 3): FAIL, the same fault. make test passes.
Related on the fix (lane 5, out/lab/20261107-193455): space_npc_model, creature_ufo_mirror, drama_guest,
creature_ufo_nomodel OK. campaign_tribe on the fix, lanes 2 and 3 (out/lab/20261107-193454): 3/3 reached Tribe (no
UFO visit in these three); #1 failed only the ground monitor gate (a puppet 0.73 m under the ground, the class of the
final check's lane 5 failure). Baseline campaign_tribe 4c1d891f lane 2 (out/lab/20261107-185422 of a 4c1d891f
worktree): 4/4 OK, no visit.

## History
- 2026-11-07: opened and fixed (fix/w46-advance-reg): not a 0.7.10 regression (v0.7.9 faults the same way).
