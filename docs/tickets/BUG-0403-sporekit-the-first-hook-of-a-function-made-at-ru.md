---
id: BUG-0403
title: SporeKit: the first hook of a function made at run time (devkit input hooks GetAsyncKeyState/GetKeyState) still writes the jmp while other threads may run the function's first bytes
status: open
kind: crash
stage: All
reported: 2026-11-01
source: found on fix/w33-null-call (BUG-0398 analysis)
role: any
versions: 0.7.3.3
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/02-modkit.md
related: BUG-0398, BUG-0202
---

## Report
Found while analysing BUG-0398, not seen at run time. BUG-0202 parked hooks write a target's 5-byte
jmp once per process and assume that first write happens at boot, before the game's threads call
the target (docs/02-modkit.md, "Parked hooks"). Some first hooks are made later:
`sporekit/input.cpp` ensure_hooks() hooks user32 GetAsyncKeyState / GetKeyState at the first
injected input (devkit `key` / `click` / `drag` / `button`, on the console thread) while the game's
main thread polls GetAsyncKeyState every frame (GameInput::IsMouseButtonDown 0x8D3340, coop's F9 and
social digit polling); coop's `ensure_model_hook()` (modes.cpp) hooks Cell SimUpdate /
GetModelKeyForCellResource at the first cell it creates (on the main thread, so no race seen).

## Root cause
Inferred from the code: MinHook (thread freeze off under Wine) writes the jmp with two stores while
another thread may be between instructions of the first 5 bytes of the target; that thread would
continue inside the jmp (the BUG-0202 crash). The window is a few nanoseconds per call (about 10
calls per frame), so it is far rarer than BUG-0398 (whose window is the microseconds of
VirtualProtect + FlushInstructionCache), and only test games make the input hooks.

## Fix
None yet. Options: make the input hooks at boot, or on the main thread (a main-thread task) so the
thread that polls is not inside the function.

## Verification
no-fail-run: not reproduced (inferred from the code; kit.test.hooknew avoids this race on purpose).

## History
- 2026-11-01: opened (fix/w33-null-call).
