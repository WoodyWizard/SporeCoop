---
id: BUG-0403
title: SporeKit: the first hook of a function made at run time (devkit input hooks GetAsyncKeyState/GetKeyState) still writes the jmp while other threads may run the function's first bytes
status: fixed
kind: crash
stage: All
reported: 2026-11-01
source: found on fix/w33-null-call (BUG-0398 analysis)
role: any
versions: 0.7.3.3
fixed_in: unreleased
branch: fix/w45-crashes
commits: 1d80c9ea
tests: kit_hooksplit
signature:
docs: docs/02-modkit.md#modkit-hook-first-write
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
Inferred from the code (2026-11-01), then **observed** (2026-11-05, fix/w45-crashes): MinHook writes
the jmp with plain stores (opcode, then operand) while another thread may be between instructions of
the first 5 bytes of the target; that thread continues inside the jmp. Reproduced with
`kit.test.hooknew 200 3 split` (functions whose first 5 bytes are five 1-byte instructions, never
hooked before, hooked while 3 threads call them in a loop): with the plain stores the game crashed
within 1 s, `0xC0000005 at 0x133E0285` (an address inside the test's code page, the jmp's operand
run as code; out/lab/20261105-225321). The real input hooks were never seen to crash this way (the
window per call is nanoseconds); the test widens it on purpose.

## Fix
`kit.fix hookfirst` (on; off = MinHook's plain stores). The vendored MinHook calls a SporeKit writer
for the patch bytes (`MH_SetPatchWriter`, hook.c EnableHookLL). After boot (frame index > 0) the
writer puts an int3 on the entry (a vectored handler sends a trapping thread to the trampoline: the
original code), suspends every other thread one at a time to read its EIP (resumed at once, only
GetThreadContext runs while it is suspended: not MinHook's freeze, which deadlocked under Wine in
VirtualProtect), waits while one is inside the patched bytes, then writes the other bytes and the
entry byte last. A class fix: every hook whose code is written after boot (input hooks, coop's Cell
hooks at the first cell, hot reloads of new targets) goes this way.
docs/02-modkit.md#modkit-hook-first-write.

## Verification
Test `kit_hooksplit` (kit_freeze.py --case hooksplit).
* FAIL `SPORE_FIX_OFF=hookfirst`: out/lab/20261105-225321 (lane 4): the game crashed during the
  first rounds (no answer, crash report crash-20261105-225333-i7.txt, AV inside the test page).
* PASS: out/lab/20261105-225351 (lane 4): 200 of 200 hooked, 59 029 103 calls on 3 threads, 0 wrong
  results, 257 644 gate traps, 0 waits, 0 refused, frames go on, no crash report.
## History
- 2026-11-01: opened (fix/w33-null-call).
- 2026-11-05 (fix/w45-crashes): reproduced with split prologues (kit.test.hooknew ... split), fixed by an int3 gate and a per-thread IP check before the bytes are written (`kit.fix hookfirst`); kit_hooksplit FAIL/PASS.
