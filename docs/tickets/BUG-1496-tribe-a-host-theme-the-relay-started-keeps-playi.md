---
id: BUG-1496
title: Tribe: a host theme the relay started keeps playing on the guest after the session ends (TR-11)
status: open
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w79-tribe
commits:
tests:
signature:
docs:
related: 
---

## Report
External static audit of 0.7.15.5, item TR-11 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
