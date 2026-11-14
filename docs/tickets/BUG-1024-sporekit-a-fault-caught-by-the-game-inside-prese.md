---
id: BUG-1024
title: SporeKit: a fault caught by the game inside Present leaves the Present depth set (no more frames) or the overlay scene open
status: fixed
kind: freeze
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-4
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit
commits: 438e1cdb
tests: kit_present_fault, kit_overlay_fault
signature:
docs: docs/02-modkit.md#modkit-present-unwind, docs/known-issues.md#issue-present-depth-stuck
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

Runtime / fix/w49-kit:
Observed at runtime (fix/w49-kit, lab lane 7, `SPORE_FIX_OFF=kit.presentunwind`):
* Path 1 (a fault inside the original Present, made by `kit.test.presentfault present` between the depth's `++` and
  o_Present, as a driver fault would be): the game's handler caught it and the game drew on, but the host's SporeKit
  frame counter stopped for good (1444 -> 1444), the outside-frame command `window` got no answer, the guest's age of
  the host's state grew to 35 s and more (lab 20261113-010320). h_Present starts a frame only at depth 0 and the game
  loop fallback returns early on a set depth (code as described above).
* Path 2 (three faults in the overlay pass, `kit.test.presentfault overlay`): worse than the audit said. The scene
  stayed open for good: the game's own BeginScene fails inside it and the game then skips its EndScene, so SporeKit's
  overlay pass never began a scene again (only 1 of 3 armed faults ever ran: no overlay pass, no labels, no partner
  arrows for the rest of the run) and the back buffer's reference count went 1 -> 3 (a later device Reset fails while
  it is held) (lab 20261113-010700-2). The first version of the fix (references released, no EndScene) showed the
  same dead overlay (lab 20261113-010320-2): the scene must be ended too.
* llvm-mingw i686 has no SEH unwinding of our frames: no destructor and no statement after the call runs.

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
sporekit/d3d9hook.cpp, `kit.fix presentunwind` (SporeKit fixes.cpp):
* The outermost Present's stack address and thread are kept (present_enter). present_cut_off: an entry of h_Present /
  h_SwapPresent on that thread at or above it (a nested Present, the device's calling the swapchain's, is deeper) or
  the game loop pass after App::Canvas::Tick (frame_from_game_loop) finds the depth still set: Present was left by an
  unwind; the depth goes back to 0, a WARN "frames: Present was left by a fault the game's handler caught (N so far)",
  the outside-frame tasks are resumed.
* The overlay pass keeps its back buffer, the game's render target and an "open scene" flag in statics; when the
  frame is found cut off (frame_cut_off, the next frame's start, before the original Present) overlay_cut_off ends the
  scene, puts the render target back and releases both surfaces (WARN "overlay: the overlay pass was cut off").
* Test commands `kit.test.presentfault present|overlay` (and its state line) and test_fault_null.
Switch off: the depth and the overlay state are left as before.

## Verification
Tests kit_present_fault (`kit_present_fault.py --case present`) and kit_overlay_fault (`--case overlay`), lanes 6 / 7:
* FAIL `SPORE_FIX_OFF=kit.presentunwind`: out/lab/20261113-010320 (present: frames 1444 -> 1444, no answer to
  `window`, the guest's age of the host 35.7 s; overlay after it: no frames) and out/lab/20261113-010700-2 (overlay:
  1 of 3 faults ran, the overlay pass dead, back buffer references 1 -> 3).
* PASS: out/lab/20261113-010700 (present: depth back to 0, frames 1594 -> 1909, `window` answers, guest's age
  0.05 s; overlay: 3 of 3 cut off, references 1 -> 1, frames go on), out/lab/20261113-011847.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit (FAIL/PASS twin).
- 2026-11-14: released in 0.7.14.
