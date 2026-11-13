---
id: BUG-1018
title: SporeKit: mod.reload / mod.unload free coop.dll while an any-thread console command of it still runs
status: open
kind: crash
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-1
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
Audit 0.7.14, KIT-1. Not seen at runtime, found by code reading. A hot reload or unload of coop.dll
(`mod.reload coop`, `mod.unload coop`, `tools/lab.py reload`) while a console command of coop.dll flagged
SK_CMD_ANY_THREAD runs on another thread unmaps the code under that command: the game crashes in unmapped memory
(an execute fault at an address of the old coop image). Only the developer console and the lab's dev loop do this;
a player never reloads mods.

## Root cause
Observed in code (main efd2e0ba), confidence high for the race, the crash itself inferred:
* sporekit/console.cpp:349-353 (start_next, the console's server thread) and console.cpp:252-253
  (command_exec_line from any thread) copy the Cmd under g_cmd_mu and call `c.fn` after the lock is gone. Nothing
  counts the calls in flight.
* sporekit/mods.cpp:72-101 (unload_from, on the main thread for `mod.reload` / `mod.unload`, both
  SK_CMD_MAIN_THREAD at commands.cpp:947-949): commands_remove_all, then `Sleep(kUnloadGraceMs)` (50 ms, mods.cpp:11)
  only when the mod had hooks and kit.fix hookkeep is on, then FreeLibrary.
* coop's any-thread commands: `coop.sample` (debug.cpp:835, samples the main thread for 100..30000 ms), `coop.census`
  (mem_census.cpp:442), `coop.cellgone` (coop.cpp:7690). `coop.sample 5000` on one console connection and
  `mod.reload coop` on a second one: the server thread is inside c_sample when the image goes.
Not a problem for SK_CMD_MAIN_THREAD / OUTSIDE_FRAME commands: they run on the main thread, which is the one
unloading.

## Fix idea
A per-mod in-flight counter (or a shared lock held for reading by run_cmd of an any-thread command and taken for
writing by unload_from after commands_remove_all): unload_from waits until the mod's running any-thread commands
have returned (with a timeout and a log line, as the update thread of BUG-0060). Switch: `kit.fix cmdkeep`.

## How to test
New `coop_reload_busy.py` (one game is enough, twoinst for the lane): console A sends `coop.sample 3000`, console B
sends `mod.reload coop` 200 ms later; assert the game is alive and answers `ping` / `coop.status` afterwards and wrote
no crash report. Main: a crash (execute fault in the freed image); with the fix: PASS. FAIL twin: `kit.fix cmdkeep 0`.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
