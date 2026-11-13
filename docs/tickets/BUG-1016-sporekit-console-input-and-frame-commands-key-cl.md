---
id: BUG-1016
title: SporeKit: console input and frame commands (key, click, move, button, frame.limit, bg) run on the console thread and touch main-thread state without a lock
status: fixed
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-7, KIT-14
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit2
commits: 702f51dd
tests: kit_cmd_fault, kit_cmd_unload
signature:
docs: docs/06-testing.md#testing-kit-cmd-unload
related:
---

## Report
Audit 2026-11-12 for 0.7.14, findings KIT-7 and KIT-14. Not seen at runtime, found by code reading. Only the test
console reaches it (players have no console client); a test that drives the mouse or the frame limit while the main
thread does the same could see a lost virtual cursor timeout, a frame limit written twice, or (in the worst case) two
first hooks of GetAsyncKeyState / GetKeyState made at once.

## Root cause
Observed in code (main efd2e0ba), confidence high for the races, low for any visible effect:
* sporekit/commands.cpp:907 (`frame.limit`), :941 (`move`), :942 (`button`), :997 (`bg`), also :935 `key`, :939
  `click`, :944 `drag`, :945 `wheel` are registered with `A` = SK_CMD_ANY_THREAD (:883) and run on the console's
  server thread.
* `frame.limit` -> focus.cpp:144 set_frame_limit -> :159 apply_frame_limit off the main thread: it writes g_forced,
  g_game_ms, g_last_seen, g_others and the game's kFrameLimitMS field, and calls game_mode_id() (gamehooks.cpp:113: a
  call into the game's mode manager and a vcall), while d3d9hook.cpp:464 runs the same function every frame on the
  main thread.
* `move` / `button` -> input.cpp:111 / :184 -> focus.cpp:217 vcursor_hold / :224 vcursor_set write g_vcursor_last
  (a plain DWORD, focus.cpp:215) while vcursor_tick (:254, main thread) reads it; g_vcursor_active and the held flag
  are atomics, the timestamp is not.
* `bg` -> focus.cpp:322 set_background_mode writes the plain bool g_background (:11) read by the window hooks on the
  main thread (:27..:78).
* KIT-14: input.cpp:32 ensure_hooks uses an unsynchronized `static bool done`; it is reached from the console thread
  (key/click/button/drag) and from the main thread (world.cpp:406, the save watcher's `click` through
  H->command_exec, run on the caller's thread). Two first calls at once make hook_create twice for the same target;
  hook_create (hooks.cpp:213) is under its own mutex and MinHook refuses the second create (inferred: harmless, the
  o_ pointer is written only on success).
Not a player path: the console is the tests' interface. Severity: Low.

Observed vs inferred (2026-11-13, fix/w49-kit2): the flags and the unguarded state observed in code (as above); no visible effect was ever seen at runtime (inferred only).

## Fix idea
Move frame.limit and bg to SK_CMD_MAIN_THREAD (they are cheap), or make set_frame_limit only store g_forced
(atomic) and let the next frame apply it. g_vcursor_last and g_background as std::atomic. ensure_hooks with
std::call_once. Switch: `kit.fix console_threads off` (the old flags).

## How to test
Hard to make FAIL by a twin test (a data race). A stress test in tools/tests (new kit_console_race.py): 2000
`frame.limit 16` / `frame.limit auto` and `move x y` from two console connections while the game runs; assert the
game's kFrameLimitMS (`sk frame` / frame_limit_now) equals the last value set and no fault is logged. Probably no
FAIL run: then `no-fail-run: data race, fixed by inspection`.

## Fix
sporekit/commands.cpp: `frame.limit` is SK_CMD_MAIN_THREAD now (set_frame_limit / apply_frame_limit and game_mode_id
run on the main thread only, as the per-frame call). sporekit/focus.cpp: g_background and g_vcursor_last are
std::atomic (written by the console thread through `bg`, `move`, `button`, `click`; read by the window hooks and
vcursor_tick on the main thread). sporekit/input.cpp: ensure_hooks runs once through std::call_once. key / click /
move / button / drag / wheel / bg stay any-thread: they only post messages and write atomics now. No switch: the old
side is a data race only, no test could use it.

## Verification
no-fail-run: data race between the console thread and the main thread on plain variables (and a first-call race
of hook_create, which MinHook already refuses twice); not reproducible as a failure, fixed by inspection. Regression:
lab runs out/lab/20261113-083757 (notify, hotreload, reload_update, kit_cmd_fault) (the console's commands in hotreload, notify, kit_cmd_fault, kit_cmd_unload) passed.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (no-fail-run, decision 4).
