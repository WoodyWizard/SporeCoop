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
fixed_in: 0.7.9
branch: fix/w45-crashes
commits: 1d80c9ea, 912afcd7, 9d9b1a1d, 66c155ce
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
original code, so nothing reaches the other patched bytes any more), waits 50 ms so a thread that was
between those bytes runs on out of them, then writes the other bytes and the entry byte last. No
thread is suspended. A class fix: every hook whose code is written after boot (the input hooks,
coop's Cell hooks at the first cell, a hot reload's new targets) goes this way; boot hooks keep the
plain stores. Left: a thread preempted inside the 5 bytes and not scheduled for the whole 50 ms.
Rejected first version (commit 1d80c9ea, replaced by 9d9b1a1d): suspend each thread and read its EIP
with GetThreadContext. Under Wine 10 that call waited forever: the host froze every time the main
thread made the input hooks in Cell (lab 20261105-225531, the "game saved" box click); a helper
thread with a timeout lost 63 reads in one Cell session. docs/02-modkit.md#modkit-hook-first-write.

## Verification
Test `kit_hooksplit` (kit_freeze.py --case hooksplit: 1000 functions whose first 5 bytes are five
1-byte instructions, hooked for the first time while 3 threads call them).
* FAIL `SPORE_FIX_OFF=hookfirst`: out/lab/20261105-232220 (3 of 3 runs): the game crashed within the
  first rounds (access violations at 0xA3FA05D0 / 0xA3FA06C8 ...: the jmp's operand run as code),
  crash reports, no answer. With 200 rounds: 2 of 3 (out/lab/20261105-225321, 20261105-231121 #1;
  #2 passed).
* PASS: out/lab/20261105-232406 (3 of 3): 1000 of 1000 hooked, 293-341 million calls, 0 wrong
  results, 6.3-7.8 million gate traps, frames go on, no crash report. Also kit_freeze (all cases,
  hooknew / hookrace / hookchurn included) 2 of 2 and kit_hooksplit (200 rounds) 2 of 2:
  out/lab/20261105-231207.
* The freeze of the rejected version is gone: cell_pause_rejoin (which clicks the game saved box on
  the main thread) out/lab/20261105-231944 PASS.

## History
- 2026-11-01: opened (fix/w33-null-call).
- 2026-11-05 (fix/w45-crashes): reproduced with split prologues (kit.test.hooknew ... split), fixed by an int3 gate and a 50 ms window before the bytes are written (`kit.fix hookfirst`; the first version with GetThreadContext froze the host under Wine and was replaced); kit_hooksplit FAIL 3/3, PASS 3/3.
- 2026-11-07: released in 0.7.9.
