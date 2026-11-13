---
id: BUG-1018
title: SporeKit: mod.reload / mod.unload free coop.dll while an any-thread console command of it still runs
status: fixed
kind: crash
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-1
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit2
commits: 702f51dd
tests: kit_cmd_unload
signature:
docs: docs/06-testing.md#testing-kit-cmd-unload
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

Observed vs inferred (2026-11-13, fix/w49-kit2): now observed at runtime: `kit.fix cmdkeep 0`, `coop.sample 4000` on the console thread and `mod.unload coop` from a second connection: exception 0xC0000005 executing at 0x74A329A8 (the old coop image) on the console thread, the game crashed (out/lab/20261113-083451).

## Fix idea
A per-mod in-flight counter (or a shared lock held for reading by run_cmd of an any-thread command and taken for
writing by unload_from after commands_remove_all): unload_from waits until the mod's running any-thread commands
have returned (with a timeout and a log line, as the update thread of BUG-0060). Switch: `kit.fix cmdkeep`.

## How to test
New `coop_reload_busy.py` (one game is enough, twoinst for the lane): console A sends `coop.sample 3000`, console B
sends `mod.reload coop` 200 ms later; assert the game is alive and answers `ping` / `coop.status` afterwards and wrote
no crash report. Main: a crash (execute fault in the freed image); with the fix: PASS. FAIL twin: `kit.fix cmdkeep 0`.

## Fix
sporekit/console.cpp: every lookup of a mod's command that runs it on the caller's thread (any-thread commands in
start_next and command_exec_line, the main-thread task lambdas) counts it as running under g_cmd_mu, the lock of the
lookup itself, and uncounts it when it returns (CmdHeld). sporekit/mods.cpp unload_from: with `kit.fix cmdkeep`
(new, sporekit/fixes.cpp) the mod's commands are removed first (no new call can start), then commands_wait_idle waits
up to 10 s for the ones running on other threads, then on_unload and the rest. A command that does not return in time
(or one on the main thread around the unload, a nested pump) keeps the old image mapped (no FreeLibrary: one DLL copy
leaks in a dev path) with a log line. The SporeKit log names each wait ("unload of coop waited N ms for 1 running
command(s) (BUG-1018)"). `kit.fix cmdkeep 0`: the old unload.

## Verification
Test kit_cmd_unload (one game: `kit.test.hang 1500`, `mod.unload coop` queued behind it, `coop.sample 4000` on the
console thread; then the same with `mod.reload coop`):
* FAIL `SPORE_FIX_OFF=kit.cmdkeep`: out/lab/20261113-083451 (crash report crash-20261113-083507-i11, execute fault in the freed image).
* PASS: out/lab/20261113-083520, out/lab/20261113-083551 (both cases waited, sample and unload answered, coop loads again, no fault).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (FAIL/PASS twin).
