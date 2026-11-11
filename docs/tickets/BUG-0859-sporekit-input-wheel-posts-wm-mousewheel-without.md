---
id: BUG-0859
title: SporeKit input_wheel posts WM_MOUSEWHEEL without the held modifier flags: Shift/Ctrl+wheel in tests zooms instead of scaling
status: open
kind: tooling
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor (their BUG-0615, open on their side)
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
## Verification

## History
- 2026-11-10: opened.
