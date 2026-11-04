---
id: BUG-0564
title: Tribe: after the guest dies, its map still shows villages that do not exist any more
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
related: BUG-0483, BUG-0539
---

## Report
The guest (3 reports): after dying it still sees other villages on the map that should not exist.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
