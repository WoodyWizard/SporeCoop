---
id: BUG-0272
title: Smoke test select is flaky: the partner is not on the screen (coop.team point 0)
status: open
kind: test
stage: creature
reported: 2026-10-29
source: lab 20261029-120208 smoke
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-select-partner-off-screen
related:
---

## Report
`select` (tools/tests/coop_select.py, smoke) fails now and then with "the partner is on the screen
(point 0 X Y)": `coop.team point` projects the partner off the screen (x 1859, 2395, 4078 or -151 on
a 1440-wide window) right after a warm reset or a fresh start. Seen on fix/w6-cell: lab
20261029-120208 (smoke, lane 4), 20261029-121511 (select#2 of 2, lane 4), and with
`SPORE_FIX_OFF=core.stale_local` too, 20261029-121818 (select#2 of 3, lane 5), so not BUG-0271's
change; earlier on other branches: 20261027-230552, 20261028-011940 (lab history).
Reproduce: `tools/lab.py run select --lane 4 --repeat 3` (about 1 in 3).
Inferred: the partner has not walked into the camera's view yet when the test asks (camera or
spawn place), a test timing problem rather than a game bug; not analysed.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
