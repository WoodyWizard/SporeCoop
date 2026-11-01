---
id: BUG-0285
title: Tribe: a handled fault in CreatureHearsTarget (citizen WalkTo) while the stage is left for the galaxy menu
status: fixed
kind: crash
stage: Tribe
reported: 2026-10-29
source: lab campaign 20261029-174717 (4 players, lane 7)
role: host
versions: 0.7.3.0 (main b5ef74d)
fixed_in: 0.7.3.2
branch: fix/w9-tribe, fix/w13-tribe-civ
commits: 22cbf89 d1cdf2e eb1bebf 4c6c6e1
tests: tribe_leave_walk (coop_tribe_leave_walk.py)
signature: 0xC0000005 at 0x00C0DB23 SporeCoop::CreatureHearsTarget+0x463 data 0x3C (handled, first chance)
docs: docs/re/tribe.md#tribe-leave-walk-fault, docs/known-issues.md#issue-tribe-leave-hears-fault
related: BUG-0284, BUG-0310
---

## Report
The 4-player campaign (lane 7, GOG 2024, host) left the Tribe stage for the galaxy menu (the jump
to the civ_npc fixture: `coop.load`, quit message posted). 0.86 s after the quit, in the frame the
mode changed, the exception logger caught a first-chance fault, handled by the game itself:
`0xC0000005 at 0x00C0DB23 SporeCoop::CreatureHearsTarget+0x463 (data 0x0000003C)`, ebx=ecx=edx=0,
called from `cCreatureBase::WalkTo+0x738 < WalkTo+0x7D0 < cCreatureCitizen::DoAction+0x496`.
Return addresses on the stack (GOG 2024): 0xBC887D (0xBC8850, called by 0xC0D591 / 0xC38707 /
0xC69777) < 0xBC87D0 (0xBC8720) < vcall < 0xD673F8 (0xD673B0) < 0xACA42A (0xACA2A0) < 0xD576F1
(0xD57680) < 0xC27BA6 (0xC27B00) < vcall < 0xC1CEF0 (0xC1CA40) < 0xC0DB10.
The game's handler unwound past the galaxy-menu reset, which left the old player empire in the
menu: the next load crashed (BUG-0284, fixed there). This fault itself is not explained.
Seen once in ~100 logged leaves to the menu (out/lab logs, 2026-10-24..29).

Reproduce: not reproduced. The campaign's tribe had been played 30 min with 4 players and raids
(`coop.tribe pincer` shortcuts, an epic creature that "went away" 0.04 s after the quit).

## Root cause
Not known. A citizen's walk action reads a null object (+0x3C) while the stage is being torn down;
whether a mod object (a puppet, a pack member, the epic) is the target is not known.

2026-10-30 (fix/w9-tribe), static reading of GOG 2024 (docs/re/tribe.md#tribe-leave-walk-fault):
* Observed (disassembly): the fault is in 0xC0DB10 (not CreatureHearsTarget: the nearest symbol),
  `thiscall(creature)`, which reads `this+0xB54` (+0x3C) unchecked. `+0xB54` is cleared only by the
  creature's cleanup 0xC1CA40 (and the constructor), so the walking citizen had been cleaned up.
* Observed: the caller chain is the game's own: 0xD673B0 -> 0xACA2A0 (its citizen branch runs only
  when the game mode is no longer Tribe, i.e. in the frame the mode became the galaxy menu) ->
  0xD57680 -> 0xC27B00 -> DoAction -> WalkTo -> 0xC0DB10. No coop.dll frame.
* Observed (lane 8, 4 leaves to the menu with 28 members, 25 of them from `coop.tribe spawn`): every
  member had +0xB54 set, no fault. The epic destroyed by the quit's save is an animal (noun
  0x18EB45E), which 0xACA2A0 handles in another branch.
* Inferred: a teardown-order race of the game (a cleaned-up citizen still updated once in the
  mode-change frame); a mod object is not shown to be involved. Not proven.

## Fix
`tribe.leave_walk` (mods/coop/gamefixes.cpp, fix/w13-tribe-civ): Citizen_OffStageCheck
(0xACA2A0) is hooked; for a citizen whose +0xB54 is 0 (cleaned up by CreatureBase_Cleanup) it is
skipped, so no new walk starts and SetMoveScale never reads the null pointer. In the Tribe mode the
check does nothing for a citizen anyway. Counters and a warning line ("was cleaned up (+0xB54 0) but
is checked for a new walk: skipped") in `coop.status` / the log. `coop.fix tribe.leave_walk off` =
the old behaviour. GOG 2016 (frozen): the symbols are missing, the guard is off
(frozen_off_922af7c7.txt). Test stand-in `coop.leavewalk` (debug.cpp, gamefixes.cpp
leave_walk_tick).
The consequence (the next load) was also contained by BUG-0284 (world.menu_player) for the player reset; BUG-0310 showed another consequence (a hanging load).

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
- 2026-10-29: opened (fix/w8-planet, found while fixing BUG-0284).
- 2026-10-30: fix/w9-tribe: the fault's function and caller chain read (game code only), not reproduced; not-reproduced.
- 2026-10-30: seen again (BUG-0310); fixed on fix/w13-tribe-civ by the guard tribe.leave_walk (stand-in test FAIL 20261030-135912, PASS 20261030-140307 2x); not-reproduced -> fixed.
- 2026-11-01: released in 0.7.3.2.
