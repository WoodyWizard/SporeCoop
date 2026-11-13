---
id: BUG-1035
title: coop / SporeKit: coop.loadworld and mod.reload / mod.unload / mod.load run inside the frame (SK_CMD_MAIN_THREAD, not outside the frame)
status: open
kind: bug
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-6
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
Audit 0.7.14, SPC-6. Not seen at runtime, found by code reading. Commands that must run outside the frame (CLAUDE.md
2: UI, saves, anything that renders) are registered as SK_CMD_MAIN_THREAD, so they run from the frame's task pump
(inside our Present detour) or from a WM_SK_PUMP message that may be dispatched inside the driver's Present:
* `coop.loadworld` (mods/coop/debug.cpp:925-949, registered at 1003-1004): closes and reopens the save database,
  ReadStarsFromSaveDB, LoadGameByName inside the frame. It also calls H->sym results and the managers without null
  checks, and widens the UTF-8 arguments byte by byte ((char16_t)(unsigned char)*s), so a non-ASCII slot path is
  wrong. A debug command for RE; no test or tool uses it.
* `mod.reload` / `mod.load` / `mod.unload` (sporekit/commands.cpp:947-949): unload_from calls coop's on_unload
  (coop.cpp:6780-6790, notify::shutdown removes the UTFWin toast windows, notify.cpp:199-204) and the new image's
  sporekit_mod_main inside the frame. Used by `tools/lab.py reload` (the dev loop); players never.

## Root cause
Observed in code (main efd2e0ba): the flags at debug.cpp:1004 and commands.cpp:947-949; command_exec_line /
start_next post SK_CMD_MAIN_THREAD to run_on_main (console.cpp:262, 360), which runs in frame_common's
pump_main_tasks (d3d9hook.cpp:466) or from WM_SK_PUMP (focus.cpp:66-68). Whether DXVK's locks are held at that point
is inferred (the frame runs before o_Present is called; the message path can be inside it).

## Fix idea
Register them with SK_CMD_OUTSIDE_FRAME (as `window`), and in c_loadworld check every H->sym / manager result and
convert the arguments with a real UTF-8 to UTF-16 conversion (MultiByteToWideChar CP_UTF8). No switch needed for the
flags (a switch would only bring the bug back); `coop.fix debug.loadworld_checks` if wanted.

## How to test
`tools/lab.py reload` with a toast on screen (`coop.notify test`) and a partner connected, ten times in a row:
assert no crash report and the toast system works after the reload (`coop.notify` status). Main: expected to pass
most runs (no FAIL seen yet): record `no-fail-run` if it cannot be made to fail. loadworld: no test (debug command).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
