---
id: BUG-0574
title: Tribe: two buildings stand right next to each other on the guest
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
related: BUG-0488
---

## Report
The guest: literally two buildings next to each other in Tribe (a picture sent to the developer).
Reports: SporeCoop-report-2026-11-04 200959 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s9694: the guest's village has its tool buildings at other places than the host's (two red-roofed huts close together, a totem missing); the host's has a totem, two huts at the bottom. Compare BUG-0523 (each game places the tools on its own, fixed in 0.7.6).
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
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:planet:tribe_tool` in 41 % of the Tribe comparisons: an NPC tribe's tools at other spots on the two planets (e.g. host only at (335 -136 353), guest only at (330 -175 341)), from the first sample of a fresh session on, labs 20261104-201842 / 20261104-203017 / 20261104-210208.
