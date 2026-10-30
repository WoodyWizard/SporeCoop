---
id: BUG-0285
title: Tribe: a handled fault in CreatureHearsTarget (citizen WalkTo) while the stage is left for the galaxy menu
status: not-reproduced
kind: crash
stage: Tribe
reported: 2026-10-29
source: lab campaign 20261029-174717 (4 players, lane 7)
role: host
versions: 0.7.3.0 (main b5ef74d)
fixed_in:
branch: fix/w9-tribe
commits:
tests:
signature: 0xC0000005 at 0x00C0DB23 SporeCoop::CreatureHearsTarget+0x463 data 0x3C (handled, first chance)
docs: docs/re/tribe.md#tribe-leave-walk-fault, docs/known-issues.md#issue-tribe-leave-hears-fault
related: BUG-0284
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
None. The consequence (the next load from the menu crashed) is contained by BUG-0284
(`world.menu_player`, 0.7.3.1).

## Verification
no-fail-run: not reproducible (4 leaves Tribe -> galaxy menu with 28 members on lane 8, scratch
probe, no fault; once in ~100 leaves in the labs); analysis only, no code change.

## History
- 2026-10-29: opened (fix/w8-planet, found while fixing BUG-0284).
- 2026-10-30: fix/w9-tribe: the fault's function and caller chain read (game code only), not reproduced; not-reproduced.
