---
id: BUG-0555
title: Creature: NPCs bite and charge the guest's creature while it flies (glides)
status: open
kind: bug
stage: Creature
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
related: BUG-0418
---

## Report
The guest: other creatures can attack it while it is flying (bite, charge, maybe strike). In the game's own rules a flying creature cannot be hit by those.
Reports: SporeCoop-report-2026-11-04 180055 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
