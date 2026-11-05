---
id: BUG-0577
title: After a rejoin the game shows the Cell and Creature stages as not done yet (stage progress lost)
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
The guest: after rejoining it shows that they need to do the cell stage and the creature stage, but they already did that.
Reports: SporeCoop-report-2026-11-04 (Discord text) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-detector2, D0 detector): the detector compares the stages done (`All:state:stage_done`, cPlayer +0x10FC..+0x110C); not seen in the D0 runs: the creature and tribe fixtures start at their stage (no trait set on either game).
