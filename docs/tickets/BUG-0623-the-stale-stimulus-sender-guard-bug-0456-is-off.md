---
id: BUG-0623
title: the stale stimulus sender guard (BUG-0456) is off: a second hook of DestroyInstance (civ.cpp BUG-0539) fails with MH_ERROR_ALREADY_CREATED
status: fixed
kind: bug
stage: All
reported: 2026-11-06
source: L4 agent, every game log since 0.7.5 (hook_create 00B22560 failed)
role: 
versions: 
fixed_in: unreleased
branch: rel/0.7.9
commits:
tests:
signature:
docs:
related: BUG-0456, BUG-0539
---

## Report
Every game log since 19d9d7e9 (2026-11-04, BUG-0539 diagnostics, released in 0.7.5) has `[ERR] coop: hook_create 00B22560 failed: MH_ERROR_ALREADY_CREATED` at startup, followed by `game fixes: stale stimulus sender guard off`: civ.cpp hooked cGameNounManager::DestroyInstance for a log line, before gamefixes.cpp hooks it for the BUG-0456 guard, and MinHook allows one hook per function. So the guard against NPC stimuli of a destroyed sender (a crash class, BUG-0456) was off in 0.7.5 to 0.7.8. Found by the L4 agent (0.7.9), 98 of 98 logs.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: two hook_create calls on 0x00B22560 (civ.cpp init runs before gamefixes::init); the second fails.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
gamefixes owns the one hook and calls a listener (gamefixes::set_destroy_listener); civ.cpp's BUG-0539 log is that listener now. No switch: there is nothing to switch back to (the old state was a failed hook).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: a hook conflict at startup, not behaviour. Before: rel/0.7.8 out/lab/20261105-123757 (play_creature host log: hook_create 00B22560 failed, guard off). After: lab 20261106-123740 (civ_tribe_left, creature): `stale stimulus sender guard on` on both games, no hook_create error, the BUG-0539 log lines still written.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
