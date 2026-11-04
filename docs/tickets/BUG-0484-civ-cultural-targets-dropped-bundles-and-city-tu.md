---
id: BUG-0484
title: Civ: cultural targets, dropped bundles and city turrets differ between the games
status: fixed
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: f2e26935 8b86a709 8ab37445 830797cd 894f4743
tests: coop_civ_turrets.py (civ_turrets)
signature:
docs: docs/re/civ.md#civ-combat-shots
related: BUG-0539, BUG-0540
---

## Report
Detector: `Civilization:unmatched:cultural_target` 9% of Civ comparisons (2 of 3 without a partner within 3 m), `unmatched:game_bundle` 5% (3 host / 5 guest), `noun_only_guest:turret` 1% and `noun_only_host:turret` 1% (a city defence turret in one game only: buildings are mirrored, turrets are not?). Classes G4 (not mirrored) and G2.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Analysed, not fixed (fix/w41-civ-combat, time box; docs/re/civ.md#civ-combat-shots):
- Turrets (observed, play_civ lab 20261104-013631: `noun_only_guest:turret` in 23 % of the comparisons, the
  host's game had none): a city's turrets are cCity +0x354 (made by 0xBE2100, placed in a slot by 0xBE39A0 /
  0xBE3AE0, the planner calls 0xBE2100 itself); they are not part of MSG_CIV_BUILD. Which call added the guest's
  own turret (its AI for an NPC city, the rebuild 0xBE5F20, the planner) is not traced. Next step: hook 0xBE39A0
  on the guest and log the caller, then block the AI caller like CreateBuilding's growth and send the host's
  turret list like the roads.
- Cultural targets (observed: 4 per city made by the city setup 0xBE7D40; unmatched only in campaign_space): their
  places are the game's own on each machine. A worldobj kind (the host's places, the guest's targets put there)
  would fit; not done.
- Bundles (observed: the Civ `game_bundle` rows lie at the old tribe village, next to the guest-only tribe rows of
  BUG-0483): leftovers of the Tribe stage in the guest's world, not bundles of a Civ fight; they go with BUG-0483.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Turrets: `civ.turrets` (fix/w42-civ-left): a guest's city growth makes no turret (City_AddTurret 0xBE39A0 from
the growth's two call sites, return addresses SporeCoop::CivGrowth_Turret_Ret1 / Ret2); the host sends the places of
one city's turrets per snapshot cycle (MSG_CIV_STATE chunk 0xFB, when changed and every 3 s) and the guest adds the
ones it lacks (City_AddTurret) and removes the ones the host does not have (City_RemoveTurret 0xBE21C0, two looks in
a row). The bundles and tribe objects went to BUG-0539, the cultural targets to BUG-0540.

## Verification
coop_civ_turrets.py (the host's game adds a turret to an NPC city; the guest's own growth gets a budget and an attack):
FAIL with SPORE_FIX_OFF=civ.turrets (the host's turret missing on the guest): out/lab/20261104-100015
(and out/lab/20261104-095739);
PASS: out/lab/20261104-095903 (and out/lab/20261104-095616).
Detector: play_civ before (lab 20261104-013631) `noun_only_guest:turret` 23 %; after, 0 % in 3 runs
(out/lab/20261104-100146, 20261104-101322, 20261104-102449). The guest's own growth buying a turret was not seen in
the test with the switch off (the half that blocks it stays unverified at runtime).

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w41-civ-combat): analysed (above), not fixed (time box).
- 2026-11-04 (fix/w42-civ-left): turrets fixed (civ.turrets); bundles / tribe leftovers -> BUG-0539, cultural targets -> BUG-0540.
- 2026-11-04: released in 0.7.6.
