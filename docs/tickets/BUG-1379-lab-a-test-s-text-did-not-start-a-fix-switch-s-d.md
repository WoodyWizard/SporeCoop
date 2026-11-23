---
id: BUG-1379
title: Lab: a test's text 'did not start' (a fix switch's description) is taken for a game that did not start: the twin tribe_chief_still_retry_off's FAILs rerun until a PASS
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-20
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w61-civ
commits: 9c86e722
tests: unit-verdict (tools/tests/labverdict_selftest.py, case env), tribe_chief_still_retry_off
signature: OK   tribe_chief_still_retry_off (its log: RESULT: FAIL; results.json log .env2.log)
docs: docs/06-testing.md#testing-final2-tribe-civ-0716
related: BUG-1347
---

## Report
Twins of the second final check of 0.7.16 (lab 20261120-130608, out/night-0.7.16-b/2-twins.txt): the twin
tribe_chief_still_retry_off (BUG-1347, `SPORE_FIX_OFF=tribe.hold_retry`, must FAIL) was listed OK (passed): "the
twin does not reproduce its bug".

## Root cause
Observed: the twin ran three times on lane 2: tribe_chief_still_retry_off.log `RESULT: FAIL` (stretches 1.7 s at
27.1 s and 1.1 s at 188.3 s, retried=0), .env1.log `RESULT: FAIL` (1.4 s at 87.3 s), .env2.log `RESULT: OK`; the
lab's verdict came from .env2 (results.json `log`). So the twin did reproduce its bug, twice. The lab retries a run
as environment trouble when `not ok and 'did not start' in text` (lab.py run_one): the test prints the SPORE_FIX_OFF
line with the switch's description, "... takes its item into the hand again when the first take in the host's
animation did not start". Every FAIL of that twin was taken for a game that did not start, retried up to twice, and
the last result kept: a probabilistic twin (the case in about one 90 s run in four) passes one run in three or so.
The same rule also retried a test whose note, FAIL line or INCONCLUSIVE reason said "did not start" (social_sing's
"the social game did not start", the harness's "(the guest menu intro did not start)").

## Fix
labverdict.env_trouble (used by lab.py run_one): only the harness's own lines: an `AssertionError: ... did not start`
(the start waits of twoinst, coop_ui and the tests' restarts), spore.sh's "the headless test display did not start",
headless.sh's "headless shell did not start", coop_long_session's "restart: the guest did not start", bwrap's "Can't
find". Self-test case `env` (make test, unit-verdict).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (before): the self-test's case env fails on rel/0.7.16 673f7a55 (no env_trouble; the old rule takes the fix
  line for environment trouble), lab 20261120-130608:tribe_chief_still_retry_off (the verdict from .env2.log).
- PASS: `python3 tools/tests/labverdict_selftest.py` on fix/w61-civ (case env, 8 checks).
- Was waiting for the lab run: tribe_chief_still_retry_off FAILs as its run's verdict (a single run; the case is
  timing, about 2 of 3 runs of 360 s showed it).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_chief_still_retry_off FAIL as its run's verdict in 20261121-000724 (stretches (45.4, 1.3) and (329.8, 1.8)); labverdict_selftest PASS in make test. Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause and fix (lab tooling).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
