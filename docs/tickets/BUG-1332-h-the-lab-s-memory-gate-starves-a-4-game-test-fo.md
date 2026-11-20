---
id: BUG-1332
title: H: the lab's memory gate starves a 4-game test for hours while 2-game tests of other lanes keep starting
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-20
source: plan
role: 
versions: 
fixed_in: unreleased
branch: fix/w59-tests
commits: bdf8814c
tests: lab_selftest.py mem_cases (make test, unit-lab)
signature:
docs: docs/06-testing.md#testing-lab-mem-reserve
related: BUG-0916, BUG-1114
---

## Report
0.7.16 final check (lab 20261119-214823, lanes 2-7, triage package H): lanes 5 and 7 ran 7 tests each. Their first
test was a 4-game campaign: lane 5 `campaign_tribe_latecut` started after 19411 s (5.4 h), lane 7
`campaign_tribe_share` after 21531 s (6.0 h); `campaign_tc2` on lane 4 waited 2860 s.

## Root cause
tools/lab.py `MemGate.admit` starts a test only when `MemAvailable - pending - need >= 8 GB`; `pending` is the games of
the tests started in the last `MEM_RAMP` (900 s). With 16-18 GB available and four lanes starting a 2-game test every
few minutes, `pending` never fell enough for a 4-game test (need 4 GB), while each 2-game test (2 GB) still fitted and
was admitted into the room the campaign waited for. The waiting lane takes no other test meanwhile (head-of-line) and
`MEM_WAIT` fails a test only when nothing of the run is running, so the campaign waited until the run thinned out.

## Fix
A reservation (`MEM_RESERVE`, `SPORE_LAB_MEM_RESERVE`, default 300 s; 0 = the old gate): a test that has waited that
long has the right of way: no other test of the run starts on any lane until it fits; several start in the order they
began to wait. A test held back only by the reservation (it would fit) never fails as "not started"; the waiting test
itself still fails after `MEM_WAIT` with nothing running (a machine too small for it). Log lines: `has waited N s: no
other test starts until it fits` and `would fit but waits: <test> waits since N s and starts first`. Worst case: the
other lanes stand still until the ramp of the last started tests runs out (15 min).

## Verification
no-fail-run: lab scheduler logic, checked by the unit self-test on a fake MemAvailable, not by a game run.
`lab_selftest.py --case mem_cases`: `mem_reserve` (a 2-game test that fits waits behind a campaign that waited 0.6 s;
memory freed: the campaign starts first, then the 2-game test; a campaign that never fits fails as not started and the
test behind it then starts) PASS on fix/w59-tests; the same self-test on the old lab.py (f6c16890, a temporary
detached worktree) FAILs 4 checks (the 2-game test starts while the campaign waits: order ['two']); the in-test twin
(reservation 0) shows the old starvation and passes as such.

## History
- 2026-11-20: opened and fixed on fix/w59-tests.
