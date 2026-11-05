---
id: BUG-0566
title: Tribe: the guest does not see an enemy village's hut (hub) health go down when the players attack it
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The guest: it does not see the enemy village's hub hit points going down.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s5771: the host's screen has the village's hut with its health bar (6/6, 375) and the fight; the guest's screen at that spot has no hut and no fight.
Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:planet:tribe_hut` in 7 % of the Tribe comparisons (huts on the guest's planet only), labs 20261104-203017 / 20261104-210208.
