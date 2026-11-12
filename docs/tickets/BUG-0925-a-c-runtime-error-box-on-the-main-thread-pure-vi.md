---
id: BUG-0925
title: a C runtime error box on the main thread (pure virtual call, abort) stops the game with nothing in the log but the box's title
status: fixed
kind: tooling
stage: all
reported: 2026-11-11
source: 0.7.12 final check, lab 20261111-044257 long_creature
role: host,guest
versions: 0.7.x
fixed_in: 0.7.12
branch: fix/w47-final12
commits: de092fa1
tests: dialog_note
signature:
docs: docs/06-testing.md#testing-dialog-note
related: BUG-0687, BUG-0926, BUG-0456, BUG-0347
---

## Report
The 0.7.12 final check's long_creature host stopped drawing (lab 20261111-044257: "FROZE", faults 0). Its log's last
lines (the keepalive's stall note of BUG-0687) list the main thread's windows: `#32770 'Wine C++ Runtime Library'`,
the C runtime's error box (a pure virtual function call R6025, abort(), an assertion). The box raises no exception,
so there was no fault line, no report, not the box's text and not the caller: the cause could not be named
(BUG-0926). The game (MSVCR90) and our DLLs (UCRT) both show this box under Wine; on Windows the same events end as
0x40000015 (BUG-0456).

## Root cause
Observed:
* SporeKit's watchdog only dumps the main thread after 30 s without frames, and the lab ends a game 10 s into a
  freeze. The keepalive note (coop.cpp `stall_note`) lists return addresses inside the exe's code only: the
  runtime's frames and a caller in coop.dll are not in it, and the words it printed (0x00610072 ...) were the box's
  UTF-16 text on the stack.
* The watchdog's way to read the main thread (SuspendThread + GetThreadContext) does not work for a thread that
  waits in a nested message loop under Wine: lab 20261111-062921-2 (the first version of this change): the suspend
  did not come back while the box was up, no dump was written, and after the box was closed the main thread drew no
  frame again. BUG-0687's "the watchdog's dump never reached the log" is the same.

## Fix
sporekit/watchdog.cpp: once the frames have stopped for 2 s the watchdog looks for a visible dialog (#32770) of the
main thread (EnumThreadWindows) and logs, once per stall, its title and the text of its controls (WM_GETTEXT with a
timeout): `watchdog: no frames for 2.0 s, the main thread shows a dialog '<title>': [Static] '<text>' ...`. It then
posts WM_SK_STACK to the game window: the main thread itself, from the box's message loop, logs the return addresses
of every module on its stack (`WATCHDOG (a dialog on the main thread): its call chain ...`, 2 KB per log line), with no
suspend. A box whose title has "Runtime Library" also writes a report (`report-*.txt`, the next start names it:
"stopped with an error message"). Nothing is dismissed. Switch `kit.fix dialognote 0`. Test command
`kit.test.dialog [ms] [report]`.

## Verification
Test `dialog_note` (tools/tests/coop_dialog_note.py, lane 5): `kit.test.dialog 6000 report` on the host.
* FAIL: out/lab/20261111-063333 (`DIALOG_NOTE_OFF=1`: no dialog line, no chain, no report); out/lab/20261111-062921-2
  (the version that suspended the main thread: no chain, no frames after the box).
* PASS: out/lab/20261111-063236 (title, text `Runtime error!||R6025|- pure virtual function call (a test)`, a chain
  over 13 modules, the report, frames afterwards).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-final12 (kit.fix dialognote).
- 2026-11-11: released in 0.7.12.
