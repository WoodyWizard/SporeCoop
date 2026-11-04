---
id: BUG-0548
title: Civ: dropped bundles (game_bundle) of fights and captures differ by one or two between the games
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: lab, coop_civ_tribe_left (fix/w42-civ-left, out/lab/20261104-131223)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0539, BUG-0484
---

## Report
coop_civ_tribe_left (fix/w42-civ-left): the bundles (game_bundle, noun 0x18C431C) differ by one or two between the
games after a capture (host 36 / guest 37, out/lab/20261104-130500) and in a warm game after other fights (34 / 35,
out/lab/20261104-131223), while the tribes, huts, tools and food mats match. The detector's `Civilization:unmatched:
game_bundle` (BUG-0539's raids aside) is this.

## Root cause
Not analysed. Inferred: a capture or a fight drops loot bundles by each game's own random roll.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened.
