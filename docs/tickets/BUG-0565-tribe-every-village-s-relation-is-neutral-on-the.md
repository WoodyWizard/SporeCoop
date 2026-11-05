---
id: BUG-0565
title: Tribe: every village's relation is neutral on the guest, while the host sees them aggressive and slightly aggressive
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
The guest: all villages are neutral to it; for the host they are aggressive and slightly aggressive.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:relation` in 34 % of the Tribe comparisons of long_tribe / play_tribe on rel/0.7.7 (0.7.6 code), labs 20261104-203017 and 20261104-210208: e.g. the tribe at (446 -150 185) -7.00 (aggressive) on the host, 0.00 (neutral) on the guest, the report's picture.
