---
id: BUG-0567
title: Tribe: a building is broken (bugged) on the guest after the guest dies
status: open
kind: bug
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
The guest: a bugged building after dying (2 reports, screenshots in the report).
Reports: SporeCoop-report-2026-11-04 190900 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s6037 / s6054: after the guest's death its screen is dark and covered with grey smoke puffs over the village, a forge smoking; the host's screen of the same village is clear.
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
