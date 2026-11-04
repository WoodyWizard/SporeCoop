---
id: BUG-0484
title: Civ: cultural targets, dropped bundles and city turrets differ between the games
status: open
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-combat-shots
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w41-civ-combat): analysed (above), not fixed (time box).
