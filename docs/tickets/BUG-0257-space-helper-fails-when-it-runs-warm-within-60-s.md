---
id: BUG-0257
title: space_helper fails when it runs warm within 60 s after a test that disabled the guest (space_stale)
status: open
kind: test
stage: Space
reported: 2026-10-28
source: 
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen in lab run 20261028-232331 (lane 3, fix/w5-space): `space_helper` ran warm right after `space_stale`, which disables the guest twice. The guest's lethal hit then destroyed its ship (`down=0 state=2`) instead of disabling it, and the host was disabled instead of destroyed. Inferred cause: `filter_own_damage` refuses a new disable within 60 s of the last one (`now - g_last_down < 60`), and the warm reset keeps the games (and that time) from the previous test. Not a game bug of this branch's changes (the same test passed cold on this branch: out/lab/20261028-235715). Fix idea: a test command that clears `g_last_down`, called by twoinst's warm reset or by the Space rescue tests at their start.


## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-28: opened.
