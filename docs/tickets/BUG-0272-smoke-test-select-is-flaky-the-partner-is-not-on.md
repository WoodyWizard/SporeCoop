---
id: BUG-0272
title: Smoke test select is flaky: the partner is not on the screen (coop.team point 0)
status: fixed
kind: test
stage: creature
reported: 2026-10-29
source: lab 20261029-120208 smoke
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w9-tests
commits: 2db65c9
tests: select
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
Inferred, not observed: right after a (warm) start the partner can stand outside the camera's view; the test read `coop.team point` once and failed. In 11 runs on fix/w9-tests the partner was always on the screen at the first read (projected x 523-880), so the off-screen case was not seen again.

## Fix
Test only (tools/tests/coop_select.py `wait_on_screen`): up to 8 s for the partner to come into view (with a 160 px margin at the top for the clicks above the root point), then `coop.team come` (our creature 3 m from the partner, the camera follows) and 6 s again, up to 3 times.

## Verification
FAIL before: lab 20261029-120208, 20261029-121511, 20261029-121818 (about 1 in 3). PASS: lab 20261030-004240 (5 of 5, lane 7) and 20261030-004746 (6 of 6, lane 6). Not verified: the `coop.team come` fallback never ran in these 11 runs.

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-tests: the test waits for the partner on the screen and walks up to it; 11 of 11 passed. Fixed (the fallback is unproven).
- 2026-11-01: released in 0.7.3.2.
