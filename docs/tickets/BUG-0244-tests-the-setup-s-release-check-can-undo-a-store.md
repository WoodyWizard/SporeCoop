---
id: BUG-0244
title: Tests: the setup's release check can undo a stored result and the BUG-0077 test cannot see it; the BUG-0142 test lacks the 0.7.2.x ANSI upgrade case
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #178 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #178: https://github.com/WoodyWizard/SporeCoop/issues/178 (the full analysis, links and the scenario are there).

**Summary**
Two tests added in release 0.7.3.0 do not guard what their tickets are about. `tests/release_tsan.cpp` (BUG-0077) runs ThreadSanitizer on `upd::ReleaseBox` alone; meanwhile the setup's check thread does a read-then-write on the box under two separate locks, which can overwrite a good release the install job just stored (a minor status glitch the test cannot catch). `test_ini_unicode` (BUG-0142) has no case for a `coop.ini` written by 0.7.2.x in the ANSI codepage, which is the file every upgrading player has. Severity: minor (setup status) and test gaps. From BUG-0077 (commit eaf0a7c) and BUG-0142 (commit 363db2b).

**Where**
- `tests/release_tsan.cpp`: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/tests/release_tsan.cpp
- `make test-tsan

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
