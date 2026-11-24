---
id: BUG-1464
title: nest_species: kill_game left the BUG-1075 start marker; the guest's restart was refused and waited 90 s, the host showed the killed guest silent (monitor gate)
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w65-tests
commits: def13b5f
tests: tools/tests/coop_creature_desync.py kill_game (nest_species, late_species, every kill_game user)
signature:
docs:
related: BUG-1075, BUG-1329
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): nest_species "FAIL lab: gate monitor: 1 on host (puppet): partner 1 (Bob) in the same place is not shown (hidden: state older than 5 s) on 2 trace lines from s 20.014"; the test's own checks passed.

## Root cause
Test harness: kill_game SIGKILLs the guest ~6 s after its join but leaves the start marker `.started.<inst>` (spore.sh, BUG-1075); spore.sh then refused the restart ("instance 12 was started on lane 6 under 20 s ago: not started twice"), restart_guest waited 90 s and started it again at 05:58:56; meanwhile the host saw the killed Bob silent (state_age 14.2 s, 24.2 s) until its timeout at 05:57:52: the monitor gate. Not the mod.

## Fix
kill_game removes the instance's start marker after the kill (as `spore.sh kill` removes all of a lane's).

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run nest_species --lane N --retry 0 --no-build` PASS without the monitor gate (the restart within seconds). no-fail-run: a test fix; FAIL before 20261123-023643.

- 2026-11-23 (0.7.16.1 final check): nest_species and late_species PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-23: opened (fix/w65-tests).
