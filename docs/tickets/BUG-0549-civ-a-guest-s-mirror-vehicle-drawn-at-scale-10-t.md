---
id: BUG-0549
title: Civ: a guest's mirror vehicle drawn at scale 10 (the host's 0.81) in play_civ
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w42-civ-left, out/lab/20261104-131223)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0485, BUG-0478
---

## Report
Detector, play_civ (fix/w42-civ-left, out/lab/20261104-131223): `Civilization:scale:vehicle` in 4 of 52 Civ comparisons:
one vehicle with scale 0.81 on the host and 10.00 on the guest. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.

## Root cause
Not analysed. The value 10 is the scale BUG-0485 saw set on commodity nodes through a shared setter (docs/re/space.md:
`scale 10 set on an object`); inferred: the same setter on a mirror vehicle (a spice claim?).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened.
