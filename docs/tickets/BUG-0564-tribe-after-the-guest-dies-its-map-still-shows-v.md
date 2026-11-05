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
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s5734, s5771, s5911, s6244: the guest's quest is still "attack the Pink village's centre" while the host's moved on to the Lavender and then the Green village; where the host fights at a village with its hut, members and health bar, the guest's screen shows an empty plaza or another place.
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
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:goal` in 28 % (the host's tribe mission manager has a current mission for a tribe, the guest's none) and `Tribe:planet:village` in 7 % (the guest's planet has 3 villages more than the host's: e.g. at (446 -150 185), where the host's tribe object keeps a centre but no placed position) of the Tribe comparisons, labs 20261104-203017 / 20261104-210208.
