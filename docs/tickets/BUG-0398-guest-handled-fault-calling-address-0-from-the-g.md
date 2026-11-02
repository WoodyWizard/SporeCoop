---
id: BUG-0398
title: Guest: handled fault calling address 0 from the game's input update (GameInput::IsMouseButtonDown+0x86 < cAppSystem::Update) during npc_walk_player
status: fixed
kind: freeze
stage: All
reported: 2026-11-01
source: found on fix/w30-npc-walk (lab 20261101-121954 npc_walk_player#1)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w33-null-call
commits: 60671e6, 793e207
tests: kit_hooknew, kit_freeze
signature: 0xC0000005 at 0x00000000 on the main thread, [esp] = sporekit.dll h_GetAsyncKeyState+0xC (after `call [o_GetAsyncKeyState]`), esi = the virtual key, eax 0 or 1; scanned "calls" (exe only) 008D33C6 (IsMouseButtonDown: call [GetAsyncKeyState IAT 0xFCC694]) or stale ones (0070693E / 007946DE 004234E8 0041DDF4 00537FF0) (GOG 2024)
docs: docs/02-modkit.md#modkit-hook-original, docs/06-testing.md, docs/known-issues.md#issue-cell-4th-player-faults
related: BUG-0309, BUG-0202, BUG-0403, BUG-0337
---

## Report
out/lab/20261101-121954-fix-w30-npc-walk npc_walk_player#1 (guest at 30 fps over SPORE_NET=player, the test's
camera right-drags on the guest): the guest's game logged a handled exception 0xC0000005 at 0x00000000 (a call
to a null pointer), `calls: 008D33C6 007EA30F 00F47947 00F47A5D 00EC6798 0088568C 00F47234 0084BD01`, called from
`GameInput::IsMouseButtonDown+0x86 < App::cAppSystem::Update+0x48F < App::cSporeApp::Shutdown+0x3A7` (nearest
symbols, likely not exact). The game went on (the test's other checks passed apart from one proxy jump). coop.dll
is not in the stack. 41 s after the start, 2 s after the guest's frame of 5.4 s (loading). Seen once in about 30
walk-test runs that day.
Again in out/lab/20261101-125335-fix-w30-npc-walk puppet_slope (guest at 30 fps over SPORE_NET=player, after
`coop.abilities goto`): 0xC0000005 at 0x00000000, `calls: 0070693E 004234E8 0041DDF4 00537FF0 004232DB 0041DD60`
(nearest symbols HierGrid_Remove+0x3B0E < App::Property::SetValueKey+0x5A8), then the guest froze (watchdog
"no frames" 30 s later) and wrote a report. Calls to address 0 also in main's lab runs 20261030-104149 and
20261031-054758 (other stacks): a family of null calls in the game, not tied to coop.dll by the stacks.

## Root cause
SporeKit's parked hooks (BUG-0202, `sporekit/hooks.cpp` stub_hook) made a NEW hook in this order:
stub `cur` = the detour, MH_CreateHook, MH_EnableHook (the target's jmp is live from here), and only
then `*original` = the trampoline. A thread calling the target in between ran the detour with its
original pointer still null; a detour that calls the original then calls address 0.
The first injected input of a test (devkit `key` / `click` / `drag` / `button`, which run on the
console thread) hooks user32 GetAsyncKeyState and GetKeyState (`sporekit/input.cpp` ensure_hooks)
while the game's main thread polls GetAsyncKeyState every frame. Its detour h_GetAsyncKeyState
calls `o_GetAsyncKeyState(vk)`: null in that window.

Observed (all four occurrences in the recorded lab runs, main + worktrees):
* 20261101-125335-fix-w30-npc-walk puppet_slope (guest, 29.6 s, the test's first `click`): the fault
  record's stack words are `793A13BC 00000031 ...`: [esp] = sporekit.dll+0x313BC, the instruction after
  `call dword ptr [o_GetAsyncKeyState]` in h_GetAsyncKeyState (0x100313B6 in that build), argument
  0x31 ('1': coop's social digit polling, social.cpp, called from coop.dll+0xECDF9 = [esp+12]);
  esi = 0x31. The exe-only "calls" scan skips sporekit / coop frames, so 0x70693E etc. are stale.
  The main thread then FROZE (watchdog 30 s later, a hang report): the fault unwound through our
  frame code into a handler of the game's (inferred: locks held by the skipped frames stay held).
* 20261101-121954-fix-w30-npc-walk npc_walk_player#1 (guest, 41.1 s, the camera right-drag): first
  exe return address 0x8D33C6 is right after `call dword ptr [0xFCC694]` (the IAT slot of
  GetAsyncKeyState) in GameInput::IsMouseButtonDown 0x8D3340; esi = 4 (VK_MBUTTON, the vk in the
  detour); eax = 0. The game went on.
* 20261030-104149 campaign#1 extra2 (BUG-0309, main 813f8cd): eax = 0, esi = 0x78 (VK_F9: coop_ui.cpp
  polls F9 every frame), the same stale tail `004234E8 0041DDF4 00537FF0` as puppet_slope; the main
  thread stopped. Same cause, inferred from the registers (no stack words kept for that run).
* 20261031-054758 campaign#1 extra1: a different bug (thousands of faults at small EIPs, BUG-0337,
  fixed in 0.7.3.2).
Only test games are affected in this form: players' games make no input hooks (no devkit input).
Every other hook made after boot (coop's Cell SimUpdate / GetModelKey hooks at the first cell, a hot
reload) had the same window.

## Fix
`kit.fix hookorig` (on): a new stub's `*original` is set as soon as MH_CreateHook returns the
trampoline, then `cur` = the detour, then MH_EnableHook; a taken-over stub (hot reload) also sets
`*original` before `cur`, with a compiler fence between. Off: the old order. Commits 60671e6 (fix),
793e207 (test).

## Verification
Test: `kit_freeze.py --case hooknew` (suite `kit_hooknew`): `kit.test.hooknew 200 3` hooks 200
functions never hooked before while 3 threads call them in a loop; each round has its own original
pointer and detour thunk; the detour counts runs with a null original (the real one calls address 0).
FAIL: `SPORE_FIX_OFF=hookorig`, lab 20261101-134920-fix-w33-null-call (lane 2): 33990 null-original
detour runs in 200 of 200 rounds.
PASS: lab 20261101-134858-fix-w33-null-call and 20261101-134925-fix-w33-null-call (lane 3): 0 in
200 rounds each, about 60 million calls.
The first version of the test (20261101-134603, 20261101-134623) crashed both ways at an eip 4 bytes
into a target: its threads were caught between instructions of the first 5 bytes by MinHook's jmp
write (BUG-0202, a different race, see BUG-0403); the targets now start with a 5-byte jmp.
The lab's own walk tests hit the original fault about once in 30 runs; not counted again here.

## History
- 2026-11-01: opened.
- 2026-11-01 (fix/w33-null-call): cause found (the detour of a new hook ran before its original
  pointer was set), fixed behind `kit.fix hookorig`, test kit_hooknew; BUG-0309 is the same cause.
- 2026-11-02: released in 0.7.3.4.
