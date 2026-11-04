---
id: BUG-0563
title: Tribe: the guest's creature from Creature stage appears as an enemy monster at the start of Tribe; killed, it leaves meat that never runs out
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
related: BUG-0345
---

## Report
The host: after starting Tribe the guest's creature appeared as an enemy monster; they killed it and its meat is infinite. On Discord a video: the creature in the cutscene was the guest's previous creature from Creature stage.
Reports: SporeCoop-report-2026-11-04 190929 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
