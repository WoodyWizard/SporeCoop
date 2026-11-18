---
id: BUG-9108
title: coop.ui show/btn/font/tree used a typed number as a window address; a control id crashed the game
status: fixed
kind: crash
stage: All
reported: 2026-11-10
source: QA crash report (TITAN QA copy, instance 9, during the BUG-9107 tooltip work)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in: 0.7.3.6-lineages42-dev
branch: feat/lineages-42
commits: 6c8c4b22
tests: QA console on a fresh QA game, see Verification
signature: "3010a413:coop.dll sui::set_flag < sui::set_visible < sui::c_ui (read 0xC00F0100)"
docs: docs/06-testing.md#test-qa-mount-package
related: BUG-9107 BUG-9103
---

## Report

crash-20261110-125749-i9: 0xC0000005 in coop.i9.5.dll+0x1DD5F4 on the main thread, read of
0xC00F0100, at the Galaxy menu 20 s after a hot reload (gen 5). 0xC00F0100 is kIdDialog, the co-op
window's control id. Developer console only: players never type coop.ui commands.

## Root cause

The SporeKit frames of the stack (build/sporekit.dll, unchanged since 0.3.6) are sk::pump_outside_tasks
-> sk::start_next -> sk::run_cmd: a console command, sent by the QA driver. `coop.ui show <hex>` (and
`btn`, `font`, `tree ... <root>`) cast the typed number straight to a window pointer; given the dialog's
control id instead of its address, set_visible -> set_flag made a virtual call through 0xC00F0100.
The exact image (.shadow/coop.i9.5.dll) was gone; its neighbours i8.5 (12:41) and i9.2 (13:09) both
place 0x1DD5F4 in sui.cpp window helpers, i8.5 in set_flag/set_visible, matching the registers (ecx =
edi = the "window").

## Fix

mods/coop/sui.cpp `arg_win`: the argument must be a window found in the live UI tree, by address
(as `coop.ui tree` prints it) or by control id; anything else prints "no window ..." and returns 1.

## Verification

QA copy (out/release36/native, profile LineagesDepthQA, instance 9, muted, 5 FPS), build with the fix:
`coop.ui show C00F0100 0` (the crashing command), `show 12345678`, `btn DEADBEEF`, `font 0BADF00D 1`
each answered "no window ..." (rc 1), game alive; `show 199253D8 1` (a real address), `show 05B598F6 1`
(a real id) and `tree 1 all 00000004` still work. No [ERR] line in sporekit.9.log.
no-fail-run: the crash itself (crash-20261110-125749-i9) is the run without the fix; not re-run on purpose.

## History
- 2026-11-10: found while triaging the QA crash reports of roadmap 42; fixed on feat/lineages-42.
- 2026-11-10: released in 0.7.3.6-lineages42-dev.
