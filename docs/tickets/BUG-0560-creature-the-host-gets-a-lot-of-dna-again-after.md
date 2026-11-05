---
id: BUG-0560
title: Cell: the host gets a lot of DNA again after going into the cell editor
status: open
kind: bug
stage: Cell
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
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
The host: after going into the editor it got a lot of DNA once again (game mode Editor in the report).
Reports: SporeCoop-report-2026-11-04 173319 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s283: the host is in the Cell editor (cell parts, "Name Your Species"), not the Creature one; the guest swims in Cell with 26 DNA, the host's editor shows 45.
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
- 2026-11-04 (fix/w43-detector2, D0 detector): the detector compares the Cell food / spent DNA (`Cell:state:dna`, `dna_spent`); not run on Cell in D0.
