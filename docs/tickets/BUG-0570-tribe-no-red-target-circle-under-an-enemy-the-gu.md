---
id: BUG-0570
title: Tribe: no red target circle under an enemy the guest targets
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-guest-player
commits: a8845587, c88909f7, 1eefde37, 998cec4f
tests: coop_tribe_target_ring.py
signature:
docs: docs/re/tribe.md#tribe-target-ring
related:
---

## Report
The guest: it does not see the red circle under an enemy when it targets it.
Reports: SporeCoop-report-2026-11-04 191506 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
* Observed (Ghidra): the attack order draws nothing (DoActionForAllSelected 0xCD0940(7, target)). The Tribe HUD's
  TribeTargetRings_Update 0xCCC7C0 draws every frame a ring (TribeTargetRing_Draw 0xCCBE90) under the combat target
  +0xE7C of every selected player citizen and erases rings nobody touched. +0xE7C is set by the citizen's AI attack
  node (0xD67980 -> SetTarget vt 0x84) only; a guest's members are mirrors without AI, its order goes to the host.
* Observed (lane 9, fix off): no ring entry under the attacked enemy in the guest's HUD (out/lab/20261104-212825).

## Fix
`tribe.cpp` (`tribe.target_ring`): the guest remembers its own attack order (target, members) and, after the HUD's
pass (a hook on TribeTargetRings_Update), draws the ring with the HUD's own TribeTargetRing_Draw while one of those
members is selected, the target lives and no other order came (120 s at most).
`coop.fix tribe.target_ring off`: no ring on the guest.

## Verification
coop_tribe_target_ring.py (suite tribe_target_ring): the guest selects its members and orders an attack
(`coop.tribe act 7`); its HUD's ring map must hold a live ring (an effect) under the enemy (TribeTargetRing_Find),
and lose it when the members are deselected.
* FAIL with `SPORE_FIX_OFF=tribe.target_ring`: out/lab/20261104-212825 (0 of 12 samples).
* PASS: out/lab/20261104-212337, out/lab/20261104-213617 (2 runs), out/lab/20261104-220621 (8 of 12 samples; the ring comes ~1 s after the order).
* Not verified: the ring's colour (the effect is the HUD's kind 0, as for the host's own attack); the ring follows
  the order, not the host's members' real target (the host does not send +0xE7C); a first version that walked the
  ring map by hand hung the guest (lab 20261104-211536, -212018: the map's owner was taken from the wrong place).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), inferred.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): fixed: the guest draws the HUD ring of its own attack order (tribe.target_ring).
- 2026-11-05: released in 0.7.7.
