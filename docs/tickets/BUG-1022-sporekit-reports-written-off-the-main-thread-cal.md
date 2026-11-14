---
id: BUG-1022
title: SporeKit: reports written off the main thread call the game's mode manager and read the mod list and coop's report text without a lock
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-3
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit
commits: 438e1cdb
tests: kit_watchdog, kit_crash, diag
signature:
docs: docs/02-modkit.md#modkit-report-safety
related:
---

## Report
Audit 0.7.14, KIT-3. Not seen at runtime, found by code reading. A hang report (watchdog thread), a crash report
(the report thread) and the fault-storm report call into the game and read SporeKit / coop state that the main
thread changes, without a lock. Effects (inferred): a report that faults while it is written (it is then cut short)
or shows a torn mods line or a co-op section from a buffer the main thread is rewriting. Players hit report paths
(hangs, crashes), but the windows are tiny: the main thread is stopped in a hang, and the coop buffer is swapped only
every 2 s.

## Root cause
Observed in code (main efd2e0ba):
* sporekit/diag.cpp:199 diag_env() calls game_mode_id() (gamehooks.cpp:113-122: IGameModeManager::Get, then the
  virtual GetActiveModeID) from whatever thread writes the report: the watchdog (diag_hang_report, diag.cpp:977,
  from watchdog.cpp:366), the crash report thread, exceptions.cpp:377 (the storm report, on the faulting thread).
* diag.cpp:202 walks mods_list() (g_mods, m->info->version) without a lock; mod.load / mod.reload change g_mods and
  m->info on the main thread (mods.cpp:72-101, 122-131). Only with a hot reload at the same time (dev only).
* mods/coop/coop.cpp:3280-3333: g_diag_buf[2] with g_diag_cur; diag_tick writes the other buffer every 2 s and flips
  the index; diag_context_fn returns c_str() of the current one; diag.cpp:430-436 copies it under g_ctx_mu (which
  coop's writer does not take). A report thread that took the pointer and is preempted for 2 s+ while the main thread
  writes twice reads a buffer being reassigned. Inferred, very unlikely.
* catch (...) in report_step (diag.cpp:718-731) does not catch SEH faults in this build (llvm-mingw, no /EHa), so a
  fault in game_mode_id on the watchdog thread goes to the crash filter (a crash report about the reporter).

Runtime / fix/w49-kit:
Observed in code as described above; no runtime failure seen (the windows are tiny: a hang stops the main thread, the
coop buffer is swapped every 2 s, the mod list changes only with mod.load / mod.reload).

## Fix idea
The main thread caches the mode id each frame (an atomic in frame_common) and diag_env reads the cache off the main
thread; a mutex (or a copy under g_cmd_mu-like lock) around g_mods for readers off the main thread; coop's report
text in a std::mutex-guarded std::string copied by diag_context_fn into a buffer owned by the caller (or a seqlock).
Switch: `kit.fix reportsafe`.

## How to test
Hard to make FAIL in the game (timing). A lab test `coop_hang_report.py` exists in spirit (watchdog report on a
stopped main thread: kit.test.hang?): assert that the hang report's mode line is the mode before the hang and the
report has the coop section. A FAIL needs a mode manager torn down at the hang (a hang inside a mode switch): try
`kit.test.hang` during a stage change; else `no-fail-run: race, not reproducible`.

## Fix
`kit.fix reportsafe`: diag_env off the main thread reads the mode the main thread caches every 10 frames
(gamehooks.cpp game_mode_cached, filled from frame_common) instead of calling IGameModeManager::Get / GetActiveModeID,
and the mods line mods.cpp keeps under a lock (mods_line, rebuilt after every load and unload) instead of walking
g_mods. mods/coop/coop.cpp: the report buffers swap under g_diag_mu and diag_context_fn returns a per-thread copy made
under it (no switch: a lock).

## Verification
no-fail-run: data race / hardening (owner decision 4, 2026-11-13); a FAIL needs a hang inside a mode switch or a hot
reload during a report. Regression: kit_watchdog (the watchdog's hang report, written off the main thread, with the
game mode and mods lines), kit_crash and diag PASS: out/lab/20261113-011055 (kit_crash, kit_watchdog, diag).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit (no-fail-run, owner decisions 4/5).
- 2026-11-14: released in 0.7.14.
