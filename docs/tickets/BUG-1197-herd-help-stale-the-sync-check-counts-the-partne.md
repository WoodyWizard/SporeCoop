---
id: BUG-1197
title: herd_help_stale: the sync-check counts the partner's puppet the test takes away on purpose (coop.puppet respawn): p 1 extra on the guest when a check falls in that window
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: lab
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
Final check of 0.7.15 (lab 20261116-025154, lane 3) and both reruns on fix/w51-t5 (lab 20261116-082817
herd_help_stale#1, #2): `herd_help_stale` FAIL on the gate `sync-check: MISMATCH ... p: 1 extra on the guest (p1)`
(guest and host); the test's own checks passed.

## Root cause
Observed: the test takes the partner's puppet away on purpose on the host for 7 s (`coop.puppet respawn 7`; already
allowed for the monitor's puppet rule, BUG-0926). The sync-check runs on the host and the guest and flags a mismatch
on two checks 3 s apart; both fell inside such a window (final check: removed ~s 41.5, MISMATCH s 45.2, back s 48.5;
rerun #2: back s 49.2, MISMATCH s 45.3): the guest's own player is then on the guest's list and not on the host's.
Not a desync of the game. `avatar_stale_target` takes the puppet away for 3 s the same way.

## Fix
tools/tests/suite.py: herd_help_stale and avatar_stale_target allow the sync-check `p extra` finding that names the
guest's own player (`match=p: 1 extra on the guest \(p1\)`), with the reason. Checked with loggates.check_logs on the
final check's logs: without the allow FAIL, with it allowed. switch: none (test only).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
