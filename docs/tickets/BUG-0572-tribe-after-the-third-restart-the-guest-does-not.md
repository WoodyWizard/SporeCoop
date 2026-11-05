---
id: BUG-0572
title: Tribe: the guest's food counter is 0 while the host's tribe has food (from about an hour into the session)
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
related:
---

## Report
The guest: after about the 3rd restart it does not see their food in Tribe.
Reports: SporeCoop-report-2026-11-04 192403 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): The guest's food counter (bottom left) is 0 while the host's is 19, 22, 56 and 29 (s6519, s6939, s9174, s9694); earlier both showed the same (15/15 s5911, 78/78 s6037, 82/82 s6244, 5/5 s6403). So the food differs from about 19:17, before the restarts.
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
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:food` once (the player tribe's food 109 on the host, 124 on the guest) in 293 Tribe comparisons, labs 20261104-203017 / 20261104-210208: the 0-vs-19..56 of the report was not reproduced in these sessions.
