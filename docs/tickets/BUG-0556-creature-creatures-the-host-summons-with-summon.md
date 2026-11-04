---
id: BUG-0556
title: Creature: creatures the host summons with Summon Flock are not friendly (green) to the guest
status: open
kind: bug
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
related:
---

## Report
Both players: the flock the host calls is not allied with the guest; the guest does not see them as friendly.
Reports: SporeCoop-report-2026-11-04 180352, 180419 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s2129 / s2144: the host's screen shows its flock with hearts (allied); at s2144 the guest's world is black (only the HUD) while the host gets "<guest> is responding again" (a guest hitch).
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
