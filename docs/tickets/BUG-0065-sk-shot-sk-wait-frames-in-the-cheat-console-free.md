---
id: BUG-0065
title: `sk shot` / `sk wait.frames` in the cheat console freeze the game (and `sk stack` for good)
status: fixed
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#43
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-sporekit
commits: 5d46dbb, 4de4d0a, 5e4dc7f
tests: tools/tests/kit_freeze.py (suite kit_freeze)
signature: cheat console 'sk shot' 15 s freeze, 'sk wait.frames' 30 s, 'sk stack' for good
docs: docs/02-modkit.md (Bridge to the Spore cheat console), sdk/include/sporekit.h SK_CMD_NOT_ON_MAIN
related: #43
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/43 (full text, discussion).

The in-game cheat `sk <devkit command>` runs every devkit command directly on the game's main thread. Commands that wait for the next frame (`shot`, `wait.frames`) then wait for something only the blocked main thread can produce: the game freezes for 15 s (`shot`) or 30 s by default (`wait.frames`) and the cheat console prints a timeout. `sk stack` and `sk hangdump` are worse: they suspend the main thread from the main thread itself, so the game never comes back. Severity: minor for `shot` / `wait.frames` (temporary freeze, typed by the player), hang for `stack` / `hangdump`.

### Where
* the cheat handler, `command_exec_line(cmd, out, true)` (from_main = true): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/cheats.cpp#L63-L97
* `command_exec_line` runs the command in place when `from_main` is set: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/console.cpp#L193-L209
* `wait.frames`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L94-L109
* `shot`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L274-L302
* `stack` (suspends `main_thread_id()`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L428-L453
* `hangdump` path (`dump_main_stack` -> `dump_thread_stack`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/watchdog.cpp#L51-L91

### How to reproduce
1. Start Spore with SporeCoop, load any game.
2. Open the cheat console (Ctrl+Shift+C), type `sk shot`, press Enter.
3. Observed by reading: the game does not draw or respond for about 15 s, then the console shows `timeout (no frames?)`.
4. `sk wait.frames` freezes for about 30 s; `sk stack` never comes back.

### Expected
Either these commands work from the cheat console (the answer arrives after the frame), or they are refused at once with a message such as "this command cannot run from the cheat console". The game never stops.

## Root cause
**What happens**

1. The player opens the cheat console (Ctrl+Shift+C) and types `sk shot`. The game calls `fc_ParseLine` on the main thread; it builds `cmd = "shot"` and calls `command_exec_line(cmd, out, true)`.
2. With `from_main == true`, `command_exec_line` calls the command function directly on the main thread (no marshalling, the `SK_CMD_*` flags are not looked at).
3. `c_shot` calls `request_screenshot(...)` and then `WaitForSingleObject(ev, 15000)`. The screenshot is taken in `capture_if_requested()`, which runs inside the Present hook on the main thread, which is now blocked in this wait. After 15 s it returns "timeout (no frames?)"; the request is still pending and is captured on the next frame afterwards, so a file appears later although the console said it failed.
4. `c_wait_frames` loops `while (frame_index() < target) Sleep(5);`. `frame_index()` is only advanced by the main thread, so it never moves; the loop ends at the timeout (30 s by default, or as long as the argument says: `sk wait.frames 1 600000` freezes for 10 minutes).
5. `c_stack` does `OpenThread(..., main_thread_id())` and `SuspendThread(t)`. Called on the main thread, the thread suspends itself and nobody resumes it: the game is frozen until it is killed. `hangdump` does the same through `dump_main_stack`.

### Confidence
CONFIRMED by reading the full path (cheat handler -> `command_exec_line(..., true)` -> the waiting command on the main thread). Not run in the game.

## Fix
Fixed on fix/w1-sporekit. A new command flag `SK_CMD_NOT_ON_MAIN` (sdk/include/sporekit.h, a new
enum value, ABI compatible) marks commands that wait for frames or stop the main thread: `shot`,
`wait.frames`, `stack`, `hangdump`. `command_exec_line` refuses them when it runs on the main
thread (the cheat console `sk ...`, a mod's `command_exec`), at once, with "'shot' waits for the
game's frames and cannot run on the game's own thread (the cheat console): use the devkit console".
Independently `thread_snapshot` and `watchdog_dump_now` refuse the calling thread itself (a thread
never suspends itself).

Switch: `kit.fix cheatwait 0` (no refusal, no self check: the old behaviour).

## Verification
Test: `tools/tests/kit_freeze.py` case `cheat`: the devkit command `cheat sk shot` (the game's cheat
manager, the same path as typing it into Ctrl+Shift+C), then `sk wait.frames 2`, `sk stack`,
`sk hangdump`; each must come back within 3 s and frames must go on.

FAIL (fixes off, `tools/tests/kit_freeze.py --old` on lane 3; manual run 2026-10-27 19:3x): `cheat sk shot ... (15.0 s)`, `cheat sk wait.frames 2: no answer (25.0 s)`, frames
68 -> 68.

PASS (fixes on): lab runs out/lab/20261027-184811 and out/lab/20261027-184904 (lane 3, all six cases PASS both times; `diag` passed in the second run, its one flaky check "a minidump is written next to it" failed in the first attempt and on main before, 2026-10-24/25). With the fix: all four come back in 0.0 s, frames go on.

### Related
Issue #28 (stack dump suspends a thread and then allocates).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #43.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: verified on today's main (88341cf: same code), fixed on fix/w1-sporekit with switch `kit.fix`; test kit_freeze FAIL with the fix off, PASS twice with it.
- 2026-10-28: released in 0.7.3.0.
