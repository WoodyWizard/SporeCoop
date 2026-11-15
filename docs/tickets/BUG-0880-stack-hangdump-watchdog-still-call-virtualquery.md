---
id: BUG-0880
title: stack / hangdump / watchdog still call VirtualQuery while the target thread is suspended, which can hang the game for good under Wine
status: fixed
kind: freeze
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-small
commits: d2728035
tests: kit_stackvm
signature:
docs: docs/06-testing.md#testing-p1-g1
related: BUG-0050, BUG-0687, BUG-0065, BUG-0791
switch: kit.stackvq
---

## Report
The BUG-0050 fix made `thread_snapshot` the only place that suspends a thread and stopped allocating there, but it still calls `VirtualQuery` on every stack region before `ResumeThread`. Under Wine a suspended thread can hold ntdll's virtual memory lock (the project saw this with MinHook's thread freeze: the next VirtualProtect hung forever). Then the console or watchdog thread blocks inside VirtualQuery and never resumes the main thread: the game stops drawing for good and writes no crash report. The automatic watchdog dump (15 s / 30 s without frames, loader lock held over 10 s) goes through this path, so the tool meant to diagnose a hang can make it permanent. BUG-0687 (4 players, host froze, "the watchdog's dump 30 s later never reached the log") already names this as the inferred cause.

Severity: medium. Confidence: medium.

## Root cause
* Observed (code): sporekit/watchdog.cpp:76 `SuspendThread(t)`, then the copy loop at sporekit/watchdog.cpp:88-96 calls `VirtualQuery(p, &mbi, ...)` for each region, and only then `ResumeThread(t)` at :100. The comment at :59-61 lists VirtualQuery as allowed while suspended.
* Observed (docs/code): docs/05-re-notes.md:191-195 ("a suspended thread may hold ntdll's virtual memory lock, and the next VirtualProtect hangs forever", why MinHook freezing is off). mods/coop/debug.cpp:471-472 (`c_sample`) avoids exactly this: the stack top comes from the TEB before suspending, and only memcpy runs while suspended.
* Observed (test): kit_freeze's `kit.test.stackheap` (sporekit/commands.cpp:659-661) holds only the CRT heap and the process heap (HeapLock), never the VM lock, so the test cannot catch this.
* Inferred (medium): Wine's NtQueryVirtualMemory takes the same virtual_mutex as VirtualProtect / VirtualAlloc, and the main thread allocates all the time, so it is sometimes suspended holding it. The MinHook case was reproduced, this exact path was not.

## Fix idea
In `thread_snapshot`, read the target's stack bounds (TEB StackBase / StackLimit via NtQueryInformationThread ThreadBasicInformation) before `SuspendThread`; while suspended call only GetThreadContext and memcpy `[esp, min(StackBase, esp + sizeof words))`, as `c_sample` does. Any VirtualQuery (frame filtering) runs after `ResumeThread`, as `describe` already does. Keep the old loop behind a kit.fix switch (for example `stackvq`).

## How to test
Extend kit_freeze with `kit.test.stackvm [stack|hangdump]`: the main thread loops VirtualAlloc / VirtualFree (or VirtualProtect) of 64 KB blocks for 5 s while the console thread runs `stack` 300 times. Assert on the game: frames keep rising (frame counter from another console connection), every `stack` answers within 2 s, no FROZE line or crash report. With the switch off the run must hang. If the hang cannot be forced under Wine in 300 tries, record no-fail-run and keep a structural assert (no VirtualQuery between Suspend and Resume, a count in the snapshot checked by the test).

## Fix
`thread_snapshot` (sporekit/watchdog.cpp) reads the target's stack bounds from its TEB (NtQueryInformationThread ThreadBasicInformation, then StackBase +4 / StackLimit +8 copied with ReadProcessMemory) **before** SuspendThread; while the thread is suspended only GetThreadContext and a memcpy of [esp, StackBase) (at most 4096 words) run; an esp outside [StackLimit, StackBase) copies nothing. VirtualQuery for the frame names runs after the resume (`describe`, as before). `ThreadSnap.vq_suspended` counts VirtualQuery calls made while suspended. `kit.fix stackvq 0` (SPORE_FIX_OFF=stackvq for kit_freeze): the old VirtualQuery loop. The same path serves `stack`, `hangdump` and the watchdog's automatic dump.
Test command `kit.test.stackvm [rounds] [ms]`: the main thread allocates, protects and frees 64 KB blocks (Wine's virtual memory lock) while the console thread takes the snapshots.

## Verification
kit_stackvm (`kit_freeze.py --case stackvm`): 300 snapshots during 5 s of VirtualAlloc / VirtualProtect / VirtualFree on the main thread; every snapshot back within 2 s, no VirtualQuery while suspended, frames go on.
* FAIL with SPORE_FIX_OFF=stackvq: out/lab/20261112-122142 ("no VirtualQuery while the main thread was suspended (600 calls)"). The hang itself did not happen in those 300 snapshots under Wine (slowest snapshot short, frames went on): the ticket's structural assert is the check, the hang stays inferred (medium).
* PASS: out/lab/20261112-122208 (300 of 300, slowest 17 ms, vq_suspended=0, 2001-2004 words copied, 494 284 allocations on the main thread meanwhile), out/lab/20261112-124219.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0050. Severity medium, confidence medium.
- 2026-11-12 (fix/w48-small): fixed (kit.fix stackvq), test kit_stackvm.
- 2026-11-12: released in 0.7.13.
