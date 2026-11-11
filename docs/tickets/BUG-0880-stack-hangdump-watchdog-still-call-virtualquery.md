---
id: BUG-0880
title: stack / hangdump / watchdog still call VirtualQuery while the target thread is suspended, which can hang the game for good under Wine
status: open
kind: freeze
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0050, BUG-0687, BUG-0065, BUG-0791
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0050. Severity medium, confidence medium.
