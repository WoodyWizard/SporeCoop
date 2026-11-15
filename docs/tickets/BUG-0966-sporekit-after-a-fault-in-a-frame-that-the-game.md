---
id: BUG-0966
title: SporeKit: after a fault in a frame that the game's handler caught, outside-frame console commands get no answer while the frames go on
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-12
source: fix/w48-rest, lab 20261112-133753-2 / -134051 / -134349 (a fault of an unreleased first version of BUG-0930)
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit
commits: 438e1cdb
tests: kit_cmd_fault
signature:
docs: docs/02-modkit.md#modkit-present-unwind
related:
---

## Report
Seen three times on fix/w48-rest while testing a first (unreleased) version of BUG-0930's fix: the host's game faulted
at 0x00C0439F (a creature's Write called with no stream: a wrong vtable slot of ours) inside a frame, sporekit logged
`frames: the frame N was cut off by a fault the game's handler caught (1 so far); frames go on (BUG-0667)`, and from
then on the host answered no `coop.puppet respawn` (an SK_CMD_OUTSIDE_FRAME command; the test's console call timed out
after 90 s) while its frames, its trace lines and the main-thread / any-thread commands (`coop.status`, `mem.read`)
went on (lab out/lab/20261112-133753-2, 20261112-134051, 20261112-134349; lane 5 and lane 4).

## Analysis (2026-11-12)
Not analysed. Candidates: the outside-frame queue's deferral (g_out_deferred, BUG-0672) not resumed after the cut-off
on that path, or a lock of the mod held by the cut frame. How to reproduce: a fault inside a mod's frame handler that
the game's own handler catches (kit.test.excfault?), then any SK_CMD_OUTSIDE_FRAME command (`coop.ui where 0`).

## Root cause
Not the Present depth (BUG-1024): the frames went on there. Inferred from the 0.7.13 logs (they are gone) and the code,
then reproduced: coop.puppet respawn is a main-thread command (MT, not SK_CMD_OUTSIDE_FRAME as the report says); its
task ran in the frame's pump (frame_common -> pump_main_tasks), its destroy went into the first version of BUG-0930's
DestroyInstance hook and faulted at 0xC0439F, the game's handler caught it ("the frame N was cut off", BUG-0667). The
task's lambda never came back, so its request was never set done: the console client waited for ever (the test's 90 s
timeout), and every later line of that connection would queue behind it. The test's next respawn faulted the same way,
so "no respawn got an answer" while new connections (coop.status, mem.read) answered.
Reproduced (fix/w49-kit, lane 7, `SPORE_FIX_OFF=kit.taskcut`): `kit.test.cmdfault`, a main-thread command whose own
task faults inside a frame: no answer in 20 s, twice (lab 20261113-010811); the frames went on.
Also seen: a fault in a task the window procedure runs (WM_SK_PUMP / WM_SK_PUMP_OUT, dispatched by
App::Canvas::Tick's message loop) is not caught by the game at all: the game ends (lab 20261113-010320-2, the first
version of the test). That path cannot hang a command; it is a crash with a report.

## Fix
sporekit/console.cpp, `kit.fix taskcut`: a console command's task notes itself (stack address, name, how to answer)
on the main thread while it runs (running_begin / running_end); command_tasks_cut_off at the next pump
(pump_main_tasks, pump_outside_tasks) or game loop pass (frame_from_game_loop) at or above that address finds a note
left behind: the task was cut off, its request is answered with "error: '<name>' was cut off by a fault the game's
handler caught" and a WARN is logged. Nested tasks (a command whose game call pumps messages) are deeper and stay.
Test command `kit.test.cmdfault` (SK_CMD_FRAME_ONLY, SporeKit-internal: only a frame's pump runs it).

## Verification
Test kit_cmd_fault (`kit_present_fault.py --case task`):
* FAIL `SPORE_FIX_OFF=kit.taskcut`: out/lab/20261113-010811 (no answer in 20 s, twice).
* PASS: out/lab/20261113-010700 (the error answer at once, the same connection answers `frame`, frames go on, the
  guest's age of the host 0.06 s), out/lab/20261113-011847.

## History
- 2026-11-12: opened.
- 2026-11-13: fixed on fix/w49-kit (FAIL/PASS twin).
- 2026-11-14: released in 0.7.14.
