---
id: BUG-0977
title: tests: walk_start_walkin with every fix off crashes in DXVK's CS thread (dc04aee5:d3d9.dll+0x11395B) after two caught faults in ucrtbase memcpy on the main thread
status: open
kind: crash
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check, lab out/lab/20261112-175447 (walk_start_walkin, lane 3)
role: 
versions: 0.7.13-rc
fixed_in:
branch:
commits:
tests: walk_start_walkin
signature: dc04aee5:d3d9.dll+0x11395B (0xC0000005, read of 0x00000004, DXVK 3.1.1 CS thread); before it dc04aee5:ucrtbase.dll+0x14A84 and dc04aee5:ucrtbase.dll+0x149B4 (caught, main thread)
docs:
related: BUG-0917, BUG-0919, BUG-0964, BUG-0667, BUG-0779
---

## Report
The rel/0.7.13 final check (rel/0.7.13 a16f6b51, lab out/lab/20261112-175447, lane 3) failed `walk_start_walkin`
(`coop_creature_walk_start.py --who solo --mode walkin --fixoff`, BUG-0964's test, docs/06-testing.md#testing-walk-start):
the host's game (instance 5) crashed 2 s after the test turned every fix off. The test died on a `coop.sync`
timeout inside its first walk-in round. The guest had left 3 s before (solo mode), no Reset, no window event, no
stage change in the log. The same test on fix/w48-walk (lab out/lab/20261112-143856-2, 649 fixes off) passed without
a fault. No other game log or crash report in 4707 lab runs (main repo and all worktrees) has a fault *in* d3d9.dll
on the main side of a test (d3d9.dll was seen only as stack words of other faults, and once in thread 516 of
fix/w8-adapters 20261029-194630 at d3d9.dll+0x43391A, a different place); no other `coop.fix all off` run (25 game
logs) had a fault after the switch.

Host log (walk_start_walkin.host.game.log), in order:

    19:37:27.953  Bob left (the test's coop.leave, solo mode)
    19:37:30.972  [WRN] coop: coop.fix all off            (all 658 fixes off, 23 unsafe skipped)
    19:37:30.990  exception 0xC0000005 at ucrtbase.dll+0x14A84 (data 0x642BBA20) tid=368 (main), esi=642BBA1C
                  calls: 00A35243 00A35152 0075F074 011F8178 0076072C 0092A6A1 007E61AF 00F47A6A ...
                  frame 1584 cut off, frames go on (BUG-0667)
    19:37:31.097  mem: the 128 MB kept for the end were given back (core.mem_reserve off)
    19:37:32.003  exception 0xC0000005 at ucrtbase.dll+0x149B4 (data 0x1D9A0000) tid=368 (main), esi=1D99FFC4
                  calls: 00A42982 00A20B2E 00A35243 00A35152 0075F074 ...   frame 1649 cut off
    19:37:32.018  exception 0xC0000005 at d3d9.dll+0x11395B (data 0x00000004) tid=512: the game crashed

The crash's .dmp is 288 bytes (no streams; the minidump was not written, as in a few other lab crashes).

## Root cause
Not determined (analysis only, no lab run: all lanes were busy). Observed:

* **d3d9.dll is DXVK 3.1.1** (32-bit): the lane's bottle SporeLab3 has `DXVK: dxvk-3.1.1` (bottle.yml), and
  `drive_c/windows/syswow64/d3d9.dll` (7 856 142 bytes, sha256 265888c3...) is byte-identical to Bottles'
  `dxvk/dxvk-3.1.1/x32/d3d9.dll`. Image base 0x62440000 (loaded at 0x79BE0000), no symbol table, RTTI names and
  .eh_frame present. No wined3d.dll in the module list.
* **Thread 512 is DXVK's CS thread** (the worker that runs the commands the D3D9 calls recorded). The chain mapped in
  the DLL (offsets from the crash report):
  * +0x29047D: a `dxvk::thread` start (calls the thread's function object).
  * +0x155E8F: the loop `cmd->exec(ctx); cmd = cmd->next` (`call *0x8(%eax)` on a linked list): DxvkCsChunk::executeAll.
  * +0x3BDFA: inside the command whose vtable at 0x62A34D80 has the RTTI name
    `dxvk::DxvkCsTypedCmd<D3D9SwapChainEx::PresentImage(unsigned)::{lambda(DxvkContext*)#1}>`: the present command the
    game's Present recorded; it calls 0x62584C80 with (0, 0) (likely DxvkContext::flushCommandList(nullptr, nullptr)).
  * +0x121C5D: releases the old command list at +0x4C, stores the new one, calls 0x62555C90 (likely
    DxvkContext::beginRecording -> DxvkCommandList::init: zeroes the submission info at +0x1C/+0x28, then takes
    `this+0x14`).
  * +0x115CB6 -> +0x11395B: 0x62553940 (likely DxvkCommandPool::getCommandBuffer, thiscall, reads this+4 = the device,
    the buffer vector at +0x10/+0x14 and the index at +0x28) called with this = null: the new command list's graphics
    pool pointer (+0x14) was 0. DXVK makes that pool in the list's constructor and never clears it; an allocation
    failure there throws (DxvkError), it does not leave null. So the command list object (a recycled or new heap
    block) did not hold a command list any more: freed or overwritten heap memory.
* **The two faults before it were in our CRT, not the game's**: SporeApp.exe imports its C runtime from MSVCR90.dll;
  sporekit.dll and coop.dll import the api-ms-win-crt-* set (ucrtbase.dll). Both faults are memcpy reads of
  unmapped memory (the source, esi+4 and a page start 0x1D9A0000) on the main thread, and both `calls:` lists are
  the chain BUG-0917 found to be "the frame above the console's dispatch" (0xA20B2E 0xA35243 ... 0x7E61AF; the list
  holds exe addresses only). Inferred: a console command of the test (sporekit or coop code) copied from a stale
  pointer. The commands the test sends after `coop.fix all off`: `coop.pauses`, `call
  Simulator::cCreatureGameData::GetAbilityMode cdecl`, the stance click, `coop.npc list`, `coop.sync`, `coop.abilities
  target none`, `coop.abilities walkto`, then `coop.sync` every 0.5 s. The 12 stack words of each fault hold no
  coop.dll (0x70530000..0x749EF000) or sporekit.dll address, so the command is not identified.
* Timing: first fault 18 ms after the switch (about one frame), second 1.0 s later, the DXVK crash 15 ms after the
  second, on the next present. Two reads of unmapped memory in one process, then a heap object of another module
  holding garbage: the pattern of a use of freed memory or a heap overwrite in the process, with DXVK as the victim.
* Not the environment: free RAM 13.9 GB at the crash (15.2 GB at start), the game's address space 499 of 4095 MB used,
  largest free block 1909 MB (the summary), no DXVK error line; DXVK's out of memory throws or logs, it does not
  null a pool. Not the device: one CreateDevice and one Reset at start only (19:36:59), no Reset, focus or
  Alt+Tab event later. Not SporeKit's frame limit or Present hooks: `coop.fix all off` switches only the coop
  registry (658 switches); the KitFix switches of sporekit/fixes.cpp (g_fix_framelimit, BUG-0779, and the others)
  are a separate list and stay on; the log shows "frame limit: the game's own 16 ms" from frame 1 and no change.
* What `all off` changes that could matter here (names new since the passing w48-walk run: civ.creature_target,
  npc.entries_listed, npc.stale_last_attacker, tribe.body_card, tribe.gameover_wait, tribe.hold_anim,
  tribe.hold_item, tribe.mirror_hold_only, world.menu_splash). In Creature right after a partner left, the off
  paths of game.listed_frame (BUG-0426), npc.entries_listed (BUG-0919: the sync entries then trust the destroyed
  byte of a pointer from the last tick's tables), game.avatar_stale_target (BUG-0918) and npc.stale_last_attacker
  (BUG-0930) are the 0.7.12-and-older code that read pointers of destroyed creatures. Which one (if any) is the
  reader here is not known.

Verdict: **unknown, most likely test-induced in the mod's process** (a stale read by a console command or by an
off-path of a fix with every fix off, then heap damage that hit DXVK). Not Wine/DXVK under memory pressure. Players
never run `coop.fix all off`; nothing points at the shipped code with fixes on.

## Fix
Not done. Plan:
1. Reproduce on one lane: `walk_start_walkin` x10 (and `--who solo --mode teleport --fixoff`), each with
   `SPORE_DETECTOR` off; then the same with fixes on (expected: no fault).
2. Find the command: log every console command with its time in lab runs (sporekit console) or let the fault
   logger print 64 stack words and resolve coop.dll / sporekit.dll return addresses (as BUG-0917 did by hand), so
   a caught fault in ucrtbase names the mod function.
3. Bisect the switch: `coop.fix all off` then `coop.fix <half> on`, halving by prefix (game., npc., core., ...),
   first the four stale-pointer fixes above.
4. Fix the found off-path or command (object list check before the copy); if the off-path itself is the old bug,
   mark that switch `COOP_FIX_UNSAFE` so `all off` skips it.
5. The empty 288-byte .dmp: check why the minidump had no streams (BUG-0127 family).

## Verification
no-fail-run: not reproduced yet (analysis only, lanes busy)

## History
- 2026-11-12: opened from the rel/0.7.13 final check (lab 20261112-175447); analysis: DXVK 3.1.1 CS thread, null command pool after two caught memcpy faults of our CRT on the main thread right after `coop.fix all off`; fix/w48-d3d9.
