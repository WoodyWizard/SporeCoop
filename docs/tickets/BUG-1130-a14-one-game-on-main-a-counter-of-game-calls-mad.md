---
id: BUG-1130
title: A14: one game::on_main(), a counter of game calls made off the main thread, a Threads section
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-guards
commits: cb1fd10d, 18d03991
tests: tools/tests/coop_threads.py (suite threads), tests/npc_tests.cpp test_off_main
signature:
docs: docs/01-architecture.md#arch-threads
related: BUG-0451, BUG-0123, BUG-1022, BUG-1018
---

## Report
Plan 0.7.15 A14 (audit CORE-6): the threading model was implicit. The main-thread id was kept four times (game.cpp's
effect hooks, set only by effects::tick and 0 before; debug.cpp; savestrip.cpp; spacecoop.cpp), nothing counted game
calls made off the main thread, and docs/01-architecture.md 1.4 had one line on it.

## Root cause
No shared main-thread check and no measurement: each module that met another thread (the UI skinner's render thread,
BUG-0451; the planet generation threads, BUG-0123) added its own.

## Fix
* `game::on_main()` / `game::main_thread()` / `game::note_main_thread()` (game.h): one id, noted by game::init and
  every frame (stage_epoch_tick, set_effect_relay_enabled). The effect hooks (10 checks), `listed()`, debug.cpp's
  `coop.sample`, savestrip.cpp (its save filter) and spacecoop.cpp's off-main note use it; the four copies are gone.
* `skg::call_probe` (sdk/include/sporekit_game.hpp): every skg helper (thiscall, cdecl_call, stdcall_call, vcall)
  calls it with the game function's address when set. coop's game.cpp installs one: off the main thread it counts the
  call per call site (coop.dll+0x... return address) and game function (mods/coop/off_main.h, portable, unit test
  test_off_main), the first call of a site writes `threads: game call 0x... from coop.dll+0x... on thread N, not the
  main thread M (A14, coop.threads)`. `coop.threads [reset|on|off|probe]` (any thread, reads under its lock) prints
  the id and the table; `probe` is the test's planted call (no game call is made).
* docs/01-architecture.md#arch-threads: the threads (keepalive timer, DNS, adapters worker, auto-update, memory walk,
  report writer, SporeKit's console / watchdog / crash threads, the game's skinner, generation and audio threads,
  Wine / D3D), what each may touch, how work reaches the main thread (run_on_main, run_outside_frame, polled
  results), the locks.
* No coop.fix switch: no behaviour change (the checks ask the same thread id; it is known from game::init instead of
  the first effects tick, which changes nothing before the first frame: the effect relay is off until then). No
  COOP_FIX, lifecycle entry, hook or message added or changed (coop.fix list, coop.lifecycle, the hook list and the
  message table stay as they were).

## Verification
- Off-main game calls in lab tests, lane 9: 0 in every game (host and guest) of creature, tribe_coop, civ_coop,
  cell_coop, space_coop (smoke members of all five stages; out/lab/20261115-021607, cb1fd10d) and tribe_play,
  civ_play, tribe_fight_sounds (Tribe / Civ skinner thread, coop.audio recording on; out/lab/20261115-022425): no
  `threads: game call` line in any run log or in lane 9's sporekit logs of those games. No off-main bug found.
- PASS: coop_threads.py out/lab/20261115-022337 (main thread known, calls=0 from boot to the galaxy menu, the planted
  console-thread call counted once at coop.dll+0x16A6EC, the main thread's not, its log line, reset).
- FAIL: the same with the probe off (SPORE_THREADS_OLD=1: `coop.threads off`, as before A14) out/lab/20261115-022356
  (3 checks fail).
- Unit: npc_tests test_off_main (per site and function, first-time report, the 64-site cap, status text, clear).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-guards (cb1fd10d, 18d03991); the lab runs 022337 / 022356 / 022425 ran the build of
  18d03991 before its commit (the same tree).
- 2026-11-18: released in 0.7.15.
