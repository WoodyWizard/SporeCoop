---
id: BUG-1209
title: A9 start: the host is launched as soon as the guest's game process exists, both games boot at once and the guest's console resets (space_planner_build)
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.15 dev
fixed_in: unreleased
branch: fix/w51-t1
commits: 39895b69, 07113651
tests: every twoinst start (space_planner_build and the rest)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-1112, BUG-0267
---

## Report
The 0.7.15 final check (lab 20261116-025154, lane 3): space_planner_build failed after 9 s, "FAIL harness:
ConnectionResetError: [Errno 104] Connection reset by peer" in twoinst._start (`guest.call('game.mode')`, the first
call after both consoles answered `ping`). Both games' logs end right after "console listening".

## Root cause
Observed: the host's and the guest's SporeKit booted in the same millisecond (05:20:36.475 both). Over the 836 starts of
that final check whose logs hold one boot each, the host boots a median 1.26 s after the guest; this start is the only
one within 0.2 s, and the only harness ConnectionResetError at a start. twoinst launches the guest, waits in
`game_process_up` (A9, BUG-1112; before: a fixed 2 s) until `spore.sh pids` lists the guest's game process, then
launches the host: the process exists before the game runs (Wine starts it), so on a loaded machine both sandboxes
start the game at once, the race on the bottle that the 2 s gap was for (BUG-0267's "two sandboxes starting together
sometimes lose one"). Inferred: the guest's game was lost that way (its console reset), as in BUG-0267. Not
reproduced on purpose: once in 836 starts.

## Fix
`game_process_up` waits until the guest's SporeKit console answers `ping` (SporeKit has booted inside the running game),
still at most the old 2 s, so the next launch never comes earlier than SporeKit's boot and never later than 0.7.14's
fixed gap. Twin: `SPORE_GAME_UP_PROCESS=1` (the process only, as A9 had it); `SPORE_START_FIXED=1` still gives the old
fixed 2 s.

## Verification
* FAIL: lab 20261116-025154 (space_planner_build, both games booted at 05:20:36.475; once in 836 starts).
* Without the fix on lane 9: space_planner_build PASS in labs 20261116-082959 and 20261116-084124 (a flake).
* PASS with the fix: labs 20261116-085443 and 20261116-091534 (every start of both: the guest's console answered before the host's
  launch).
* no-fail-run: once in 836 starts, not reproducible on purpose; twin `SPORE_GAME_UP_PROCESS=1` keeps the A9 wait.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
