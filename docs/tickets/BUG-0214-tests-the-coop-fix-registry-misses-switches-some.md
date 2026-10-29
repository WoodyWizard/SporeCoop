---
id: BUG-0214
title: Tests: the coop.fix registry misses switches, some switches turn off several fixes, and the registry check cannot see it (BUG-0116 incomplete)
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #148 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #148: https://github.com/WoodyWizard/SporeCoop/issues/148 (the full analysis, links and the scenario are there).

**Summary**

BUG-0116 (commits 4099ebc, dd3c506, ed70db7, release 0.7.3.0) promised one registry, `coop.fix`, for every fix switch, with a source scan that fails `make test` when a fix bool is left out. That promise is not kept:
- several switches are not registered, and the scan cannot find them;
- some switches turn off more than their own fix (`session.end`, `tribe.chiefs`, the updater's `g_old_fix`);
- `net_tests_asan --nofix` aborts partway, so most later fix tests are never seen failing under ASan;
- BUG-0047 has no switch at all;
- 29 tickets name switch names that `coop.fix` answers with "unknown fix".

Severity: low. It affects only tests and the lab: a twin run (`coop.fix X off`, `SPORE_FIX_OFF`, `coop.fix all off`) can miss a fix or show the wrong bug. Players are not affected.


## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
