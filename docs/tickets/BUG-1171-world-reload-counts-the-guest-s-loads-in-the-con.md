---
id: BUG-1171
title: world_reload counts the guest's loads in the console's last 400 log lines and reads a warm game's old swap count: FAIL without a fault
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: lab 20261115-164230 (feat/w51-core2)
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-core2
commits: 74d13db9
tests: world_reload
signature:
docs:
related: BUG-0119, BUG-1141
---

## Report
world_reload FAILED in lab 20261115-164230 (feat/w51-core2, lane 9, warm): "before: the guest plays its own creature"
and "round 2: the guest loaded the host's world again", although the guest's game log shows the round 2 load and the
own creature applied 3 s after it (round 2's own check passed).

## Root cause
The test, not the mod. (1) "before" waited for `avatar_swaps >= 1`, which a warm game already has from the test before
it, and then read the avatar before this session's swap (it came 25 s in). (2) The loads were counted in the console's
`log 400` ring: once the round-1 load line left the last 400 lines the count did not grow (a busier log: C2 / C3 add a
line per world change and per host-busy message).

## Fix
tools/tests/coop_world_reload.py: "before" waits for the own creature itself; the loads are counted in the guest
game's whole log file (`lane.game_log`, as editor_strays does). No check loosened.

## Verification
FAIL (the old test) 20261115-164230:world_reload; PASS (the fixed test, same build) 20261115-174822:world_reload.

## History
- 2026-11-15: opened.
