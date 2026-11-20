---
id: BUG-1359
title: Lab: a cold start after a test goes on while the lane's old host still holds its console port (evolve_world JSONDecodeError)
status: open
kind: tooling
stage: Tools
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in:
branch: fix/w60-misc
commits: 52a307a4
tests: evolve_world
signature:
docs: docs/06-testing.md#testing-ports-after-kill
related: BUG-1075, BUG-1112, BUG-0267
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 4): evolve_world FAIL after 9 s with a Python traceback in the
harness: twoinst.wait_up -> sporectl Console.call('ping') -> json.decoder.JSONDecodeError "Expecting value: line 1
column 1". The same traceback ended own_space in 20261118-110806 (rel/0.7.15.1) and campaign_tribe_end in
20261114-005006; history of evolve_world: 1 of 8 failed on rel/main, 5 of 27 on all branches.

## Root cause
Observed: the previous test on lane 4, rejoin_saved_puppet, had its host (instance 7, pid 396, console 47806) logging
until 08:01:00.5; evolve_world's cold start booted the guest at 08:01:04 (47807 free: it listened) and the host at
08:01:05.7, whose log says "console: cannot listen on 127.0.0.1:47806 (err 10013)": the port still had a listener.
twoinst's `lane_down()` (A9, BUG-1112) waits for the lane's processes to go after `spore.sh kill` at most 2 s (the old
fixed sleep) and the start went on; `wait_up` then reached the old listener, whose answer was an empty line (not JSON),
and only OSError was caught, so the test died. The same "cannot listen ... (err 10013)" is in the host logs of the
other two runs. Inferred: the killed game (a Wine process tearing down) kept its listening socket for some seconds.

## Fix
tools/tests/twoinst.py: `lane_down()` waits, after the processes, until neither console port of the lane (47800 +
instance - 1, host and guest) has a listener, at most 20 s more (a note in the test log when it had to wait); not under
SPORE_START_FIXED=1 (the old fixed sleep). `wait_up()` counts a reply that is not JSON as "not up yet" (with the
start's own "did not answer in 90 s: the lane starts again" path behind it) instead of dying.

## Verification
no-fail-run: harness change (the lab's start); the case needs a killed game that keeps its port, which no test makes
on purpose. Lab tonight: evolve_world PASS; any cold start that prints "console ports ... were still held after the
kill" shows the wait at work. Open, waiting for the lab run.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in twoinst.py on fix/w60-misc.
