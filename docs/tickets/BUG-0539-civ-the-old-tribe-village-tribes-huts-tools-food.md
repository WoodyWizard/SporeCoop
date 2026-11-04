---
id: BUG-0539
title: Civ: the old tribe village (tribes, huts, tools, food mats, bundles) stands in the guest's Civ world only
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w42-civ-left, out/lab/20261104-100146)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-hp-shots-turrets
related: BUG-0483, BUG-0484
---

## Report
Detector, play_civ on civ_npc (fix/w42-civ-left, out/lab/20261104-100146): `Civilization:noun_only_guest:tribe`,
`tribe_tool`, `tribe_hut`, `tribe_food_mat` (0 in the host's game, 11 in the guest's, whole list) in 2 of 30 Civ
comparisons, and `unmatched:game_bundle` (2 host / 5 guest, 3 guest without a partner) in 3 of 30. BUG-0484 already
placed the bundles at the old tribe village (-234 -34 482). Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.

## Root cause
Not analysed. BUG-0483 (fixed in 0.7.5) stopped a guest's own tribe forming in Tribe and left its Civ rows to the
Civ side. Inferred: the host's game removes (or never makes) the last Tribe stage's objects in the Civ world, the
guest's game, which loaded the host's world, keeps them.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened (split from BUG-0484's bundles).
