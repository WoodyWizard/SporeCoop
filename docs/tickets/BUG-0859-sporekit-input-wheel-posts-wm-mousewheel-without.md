---
id: BUG-0859
title: SporeKit input_wheel posts WM_MOUSEWHEEL without the held modifier flags: Shift/Ctrl+wheel in tests zooms instead of scaling
status: fixed
kind: tooling
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor (their BUG-0615, open on their side)
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-small
commits: d2728035
tests: kit_wheel
signature:
docs: docs/06-testing.md#testing-p1-g1
related:
switch: kit.wheelmk
---

## Report
Test tooling, no player impact. A lab test that holds Shift or Ctrl (SporeKit key input) and then sends the virtual
mouse wheel gets a plain wheel: in the creature editor the camera zooms instead of the part scaling.

## Root cause
Observed in code (confidence high): `sporekit/input.cpp:134` posts `WM_MOUSEWHEEL` with `MAKEWPARAM(0, delta)`: the low
word (MK_SHIFT / MK_CONTROL / button flags) is always 0, whatever keys the test holds. The fork found the same in its
editor tests (their BUG-0615, still open there, no fix).

## Fix idea
From the fork (jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor, their BUG-0615); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
The fork only diagnosed it. Idea: keep the virtual modifier / button state the input commands set (the held keys of
`input_key` down / up) and put MK_SHIFT, MK_CONTROL, MK_LBUTTON, MK_RBUTTON into the low word of wParam.

## How to test
Lab, creature editor with a part selected: virtual Shift down, wheel 3 notches, Shift up. FAIL: the camera distance
changes and the part scale does not; PASS: the part scale changes and the camera stays.

## Fix
Root cause confirmed (`MAKEWPARAM(0, delta)`). `input_wheel` (sporekit/input.cpp) puts the held virtual keys and buttons into the low word of WM_MOUSEWHEEL's wParam at the moment each notch is posted: MK_SHIFT (VK_SHIFT / LSHIFT / RSHIFT held by `key ... down`), MK_CONTROL, MK_LBUTTON / MK_RBUTTON / MK_MBUTTON (held by `press`). `kit.fix wheelmk 0`: 0 as before. Test command `kit.test.wheel`: the last WM_MOUSEWHEEL the game window's subclassed procedure (focus.cpp h_WndProc) received.

## Verification
kit_wheel (`kit_freeze.py --case wheel`, one game at the menu): `key shift down`, `wheel 720 540 1`: the game window gets MK_SHIFT; the same with Ctrl: MK_CONTROL; a plain wheel: no flag.
* FAIL with SPORE_FIX_OFF=wheelmk: out/lab/20261112-122236 (keys=0x0000 with Shift and with Ctrl held).
* PASS: out/lab/20261112-122255 (keys=0x0004 / 0x0008 / 0x0000), out/lab/20261112-124219.
The editor part-scaling check of the report was not run (no editor test needs it now); the test asserts the message the game's window procedure receives, which is what the game reads.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-small): fixed (kit.fix wheelmk), test kit_wheel.
- 2026-11-12: released in 0.7.13.
