---
id: BUG-0558
title: Creature: the UFO that picks up the players at the end of Creature cannot pick up the guest (the guest does not see the ship)
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
related: BUG-0486
---

## Report
Both players: the spaceship tries to pick up the guest but cannot; the guest does not see the ship.
Reports: SporeCoop-report-2026-11-04 182712, 182720 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s3524 / s3528: the guest stands on grass under the moon; the host's screen at the same second shows another place (blue, plants like the sea floor): the two games are not at the same spot when the ship comes.
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
