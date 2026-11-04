---
id: BUG-0557
title: Creature: the guest cannot eat some bodies of creatures the players killed
status: open
kind: desync
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0383
---

## Report
Both players: the guest cannot eat some of the bodies of creatures they killed together.
Reports: SporeCoop-report-2026-11-04 181848, 181852 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s3016 / s3024: the host sees two bodies of killed creatures by a nest; the guest's screen at the same time shows that ground without them (a darker view).
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
