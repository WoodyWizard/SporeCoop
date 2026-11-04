---
id: BUG-0559
title: Creature: parts of a pack the guest lacks (Creepy & Cute) are drawn as big blocks, and the host's colours are another shade on the guest
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
related: BUG-0424, BUG-0452
---

## Report
The host: the guest sees some parts of the host's creature as big blocks (different from part to part); the guest does not have the Creepy & Cute pack. On Discord: the host's colour is a completely different shade on the guest's screen. Before 0.7.4 this crashed; now a fallback part is drawn, it should be a similar part.
Reports: SporeCoop-report-2026-11-04 184642 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
