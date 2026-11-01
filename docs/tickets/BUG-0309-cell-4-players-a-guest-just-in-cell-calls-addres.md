---
id: BUG-0309
title: Cell, 4 players: a guest just in Cell calls address 0 from UI code right after its test window is placed, then its main thread stops (no log, no frames)
status: duplicate
kind: freeze
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign#1
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests: campaign
signature:
docs:
related: BUG-0398
---

## Report
Lab 20261030-104149 campaign#1 (main 813f8cd, lane 2), guest Dave (instance 32, the 4th player): 6 s
after loading into Cell with 205 mirrors, 0.16 s after `sporekit: test window placed in the grid
(10080,0)`, `0xC0000005 at 0x00000000 (data 0)` with eax=ebx=0, stack words `007946DE 004234E8 ...`
named `App::Property::SetValueKey+0x5A8 < Math::MultiplyVectorScalar+0x194 < ? <
UTFWin::Window::func19+0x25D4`. Nothing more in its game log for 4.5 minutes (no 10 s trace, no
watchdog line) until the test closed the session; the campaign saw "FROZE (no frame for 10 s)".

## Root cause
Not known. Observed: a jump or return to address 0 on the main thread during UI / window work
right after the window was moved; the main thread made no progress after it. Inferred: a null
callback, or a `ret` to a zeroed stack slot; the stack words are a scan, not a chain. Whether the
test harness's window move (`test window placed in the grid`) matters is not checked: no other
log of the lab history has a fault within 3 lines after that line. Load: the machine was
overloaded (host fps 17-21).

## Fix
Not fixed.

## Verification
Not reproduced on purpose yet.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w13-cell): not reproduced. The stack words are a scan: 0x7946DE is the return
  address after `call 0x11E077E` (a memset-like call: size, 0, pointer) in the function at ~0x7946B0,
  0x4234E8 the return after `call 0x41CB30`; neither calls through a pointer, so they are stale
  frames, not the caller of address 0. In the lab history 5683 "test window placed in the grid" lines
  are followed by no fault except this one. Nothing more to go on without a dump.
- 2026-11-01 (fix/w33-null-call): same cause as BUG-0398 (inferred): eax = 0 and esi = 0x78 (VK_F9,
  which coop_ui.cpp polls every frame through GetAsyncKeyState) are the registers of SporeKit's
  h_GetAsyncKeyState calling its still-null original while an injected input of the campaign bot
  hooked GetAsyncKeyState (whether a devkit input came at that moment is not checked); the stale "calls" tail `004234E8 0041DDF4 00537FF0` is the one of the
  BUG-0398 puppet_slope fault, whose stack words show the detour's frame. Fixed there
  (`kit.fix hookorig`).
