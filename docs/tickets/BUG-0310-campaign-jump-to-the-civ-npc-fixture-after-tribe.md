---
id: BUG-0310
title: Campaign jump to the civ_npc fixture after tribe: faults while leaving tribe and in cSpaceGfx init, the host never finishes loading
status: fixed
kind: bug
stage: Tribe/Civ
reported: 2026-10-30
source: lab 20261030-104149 campaign#3
role: host
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w13-tribe-civ
commits: 22cbf89 d1cdf2e eb1bebf 4c6c6e1
tests: tribe_leave_walk (coop_tribe_leave_walk.py)
signature: 0xC0000005 at 0x00C0DB23 CreatureBase_SetMoveScale+0x13 data 0x3C < WalkTo < DoAction+0x496 < Citizen_IdleWalk < Citizen_OffStageCheck+0x18A; then 0x00C70A20 cPlanet::GetRingEffect+0x90, 0x010350D7 cSpaceGfx_EnableHeatEffects+0x847 in the next load
docs: docs/re/tribe.md#tribe-offstage-action, docs/re/tribe.md#tribe-leave-walk-fault, docs/06-testing.md#testing-tribe-leave-walk, docs/known-issues.md#issue-tribe-leave-hears-fault
related: BUG-0284, BUG-0285, BUG-0305
switch: tribe.leave_walk
---

## Report
Lab 20261030-104149 campaign#3 (main 813f8cd, lane 7): tribe did not end in 20 min (the raids left
4 other tribes; members spawned for the raids), so the host jumped to the civ_npc fixture
(`coop.load`, quit to the galaxy menu). 1.4 s after the quit: `0xC0000005 at 0x00C0DB23
CreatureBase_SetMoveScale+0x13` from a citizen's DoAction; then while loading the fixture
`0x00C70A20 cPlanet::GetRingEffect+0x90` (cSpaceGfx::InitializeInternal) and `0x010350D7
cSpaceGfx_EnableHeatEffects+0x847`; `the host did not load the civ_npc fixture within 180 s`.
The campaign's tribe sync also showed kind t (tribe members) 4.1-4.8 m off on all three guests at
every check from 11:07 (BUG-0305 saw kind n in the run before).

## Root cause
Observed (host game log): the same fault as BUG-0285 (0xC0DB23, a citizen's +0xB54 null, chain
Citizen_OffStageCheck -> Citizen_IdleWalk 0xD57680 -> 0xC27B00 -> DoAction -> WalkTo ->
CreatureBase_SetMoveScale), 1.4 s after the quit, in the frame the mode became the galaxy menu.
Observed (lab, stand-in): the same fault in the frame Tribe is left makes the next load hang
(the host stops drawing): the game's handler unwinds past the rest of the stage change.
Observed (static): Citizen_OffStageCheck is called by the end (0xD673B0) and the tick (0xD69740)
of the citizen action 0x5218BB3 (descriptor at 0x1583F40) and by 12 more functions; its citizen
branch acts only outside the Tribe mode. docs/re/tribe.md#tribe-offstage-action.
Inferred: a citizen that the stage's teardown had already cleaned up (CreatureBase_Cleanup, the
only writer of +0xB54 = 0) still ended its action in the mode-change frame. Game code only (no
coop.dll frame); co-op makes it likelier (long sessions, many members, raids). The 4.1-4.8 m kind t
sync offsets of the same run are not part of this ticket (BUG-0305's family).

## Fix
`tribe.leave_walk` (mods/coop/gamefixes.cpp, fix/w13-tribe-civ): Citizen_OffStageCheck
(0xACA2A0) is hooked; for a citizen whose +0xB54 is 0 (cleaned up by CreatureBase_Cleanup) it is
skipped, so no new walk starts and SetMoveScale never reads the null pointer. In the Tribe mode the
check does nothing for a citizen anyway. Counters and a warning line ("was cleaned up (+0xB54 0) but
is checked for a new walk: skipped") in `coop.status` / the log. `coop.fix tribe.leave_walk off` =
the old behaviour. GOG 2016 (frozen): the symbols are missing, the guard is off
(frozen_off_922af7c7.txt). Test stand-in `coop.leavewalk` (debug.cpp, gamefixes.cpp
leave_walk_tick).

## Verification
`coop_tribe_leave_walk.py` (suite `tribe_leave_walk`): the tribe fixture, 90 s of play_tribe
bots, then the campaign's jump to civ_npc through the galaxy menu; in the first frame Tribe is left
the stand-in clears one player member's +0xB54 and puts it through the check (the game's own
trigger, a citizen in action 0x5218BB3 as the mode changes, did not come in 90 s of play:
docs/re/tribe.md#tribe-offstage-action).
* FAIL with the fix off (`SPORE_FIX_OFF=tribe.leave_walk`, lane 6): lab run
  20261030-135912-fix-w13-tribe-civ: the campaign's fault `0xC0000005 at 0x00C0DB23
  CreatureBase_SetMoveScale+0x13 data 0x3C` with the same chain (DoAction+0x496 < 0xD576F1
  Citizen_IdleWalk), then the host stopped drawing in the load (FROZE), the civ_npc fixture never
  came: the campaign's outcome.
* PASS with the fix: lab runs 20261030-140307-fix-w13-tribe-civ (2 runs, lane 6) and 20261030-145455-fix-w13-tribe-civ (final code): 4 leaves of Tribe
  with the stand-in, "cleaned-up citizen walks skipped" 2 per run, no fault, every load reached
  Civ / Tribe and the guest followed.
Not verified: which citizen the game cleaned up while its action still ran, and why only in the
30-min 4-player campaigns (2 of ~100 leaves); the stand-in makes that state, it does not find it.

## History
- 2026-10-30: opened.
- 2026-10-30: fixed on fix/w13-tribe-civ (tribe.leave_walk), stand-in test FAIL 20261030-135912, PASS 20261030-140307 (2x).
- 2026-11-01: released in 0.7.3.2.
