---
id: BUG-1024
title: SporeKit: a fault caught by the game inside Present leaves the Present depth set (no more frames) or the overlay scene open
status: open
kind: freeze
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-4
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-present-depth-stuck
related:
---

## Report
Audit 0.7.14, KIT-4. Not seen at runtime, found by code reading. Two paths:
1. A fault that the game's own handler catches while our Present detour is on the stack below o_Present (a fault
   inside the driver's Present, or in a window message dispatched inside Present: SporeKit's WM_SK_PUMP runs the
   main-thread tasks, i.e. console commands and mods' run_on_main tasks, focus.cpp:66-68) unwinds past
   `g_present_depth--`: the depth stays >= 1 for the rest of the run. Then h_Present never starts a frame again
   (`g_present_depth == 0` is its condition), frame_from_game_loop returns early on g_present_depth, in_frame() is
   true for good and the outside-frame tasks wait for ever. The game draws on, the mod is dead: no co-op ticks, no
   network, no console answers but any-thread ones.
2. A fault in an SK_EV_OVERLAY handler (coop's labels, arrows, the find overlay) skips EndScene, the SetRenderTarget
   back and the two Release calls (d3d9hook.cpp:497-510): the next BeginScene of the game is called inside an open
   scene and the back buffer / render target refs leak once per such fault.
A player can hit (1) only through another fault (a bug of ours or the game's) that lands inside Present; (2) through
a fault in an overlay handler. Not BUG-0966: there the frames went on after the cut-off; with (1) no frame runs.

## Root cause
Observed in code (main efd2e0ba), the reachability of (1) inferred (that the game's handler catches a fault from a
message dispatched inside Present):
* sporekit/d3d9hook.cpp:588-598 (h_Present) and 600-617 (h_SwapPresent): plain `g_present_depth++` / `--` around the
  original call, no unwind-safe reset. frame_cut_off (d3d9hook.cpp:419-430) clears g_in_frame / g_in_loop_frame but
  not g_present_depth, and runs only from paths that themselves require g_present_depth == 0 (h_Present's
  on_frame_begin_present, frame_from_game_loop at 524).
* d3d9hook.cpp:711 in_frame() includes g_present_depth > 0; console.cpp:75-81 defers outside-frame tasks while
  in_frame().
* The C++ guards would not help: llvm-mingw i686, SEH unwinding by the game's __except skips our frames without
  running destructors.

## Fix idea
Record the stack pointer and thread of the outermost Present entry (as g_frame_sp does for frames); in
frame_from_game_loop (after App::Canvas::Tick, which runs outside Present) a set g_present_depth on the main thread
means Present was left by an unwind: reset it to 0, log it once per run, resume the outside tasks. For the overlay:
run the SK_EV_OVERLAY subscribers inside SporeKit's own SEH guard (hooks.cpp / exceptions.cpp have the machinery) or
end the scene and restore the target in the cut-off path (keep old_rt / bb in statics released there). Switch:
`kit.fix presentunwind`.

## How to test
A test command that faults inside Present: `kit.test.excfault present` (new: a run_on_main task posted so that it is
pumped from a message inside Present, or a fault in an SK_EV_OVERLAY handler of a test mod) with the game's handler
catching it. New `coop_present_fault.py` (one lane): after the fault assert `frame` keeps counting for 5 s and an
outside-frame command (`coop.ui where 0`) answers. Main: the frame counter stops (path 1) / the log shows BeginScene
failing (path 2): FAIL. Related BUG-0966 (frames go on there; check its candidates at the same time).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
