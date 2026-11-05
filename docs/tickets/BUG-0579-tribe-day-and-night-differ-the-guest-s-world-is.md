---
id: BUG-0579
title: Tribe: day and night differ: the guest's world is at night while the host's is in daylight
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build), screenshots
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0569
---

## Report
Seen only on the screenshots of the 2026-11-04 reports (SporeCoop-report-2026-11-04 190900, 191227, 191438, guest and host at the same session second): s6054, s6244, s6375, s6403: the guest's sky and ground are dark (night) while the host's screen at the same second is in daylight; at s5911 and s6037 both were in daylight. The players did not mention it.
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
- 2026-11-04 (fix/w43-detector2, D0 detector): `All:state:time_of_day` in 16 % of all comparisons, long_tribe only (lab 20261104-203017): from about 10 min into the session the host's sky stood still (q (-0.08 -0.08 -0.06 0.99) in consecutive samples) while the guest's moved, 25 deg apart growing to 145 deg.
