---
id: BUG-1498
title: Net: a stale rejoin slot token is used before the kept one after a crash (NET-2)
status: open
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w80-net
commits:
tests:
signature:
docs:
related: BUG-1135
---

## Report
External static audit of 0.7.15.5, item NET-2 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
