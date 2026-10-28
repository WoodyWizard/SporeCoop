---
id: BUG-0050
title: stack dump (stack, hangdump, watchdog) can freeze the game for good
status: fixed
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#28
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-sporekit
commits: 5d46dbb, 4de4d0a, 5e4dc7f
tests: tools/tests/kit_freeze.py (suite kit_freeze)
signature: stack / hangdump never answer; the main thread stays suspended
docs: docs/02-modkit.md (Diagnostics, commands), docs/06-testing.md#testing-kit-freeze
related: #28
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/28 (full text, discussion).

The console commands `stack` and `hangdump`, and the hang watchdog, suspend a game thread and then allocate memory before they resume it. If the suspended thread holds the C runtime heap lock at that moment (any `malloc` / `new` in progress), the dumping thread blocks forever and the suspended thread is never resumed. The game is frozen for good, and the tool meant to diagnose hangs creates one. Severity: hang.

### Where
* `c_stack` (console command `stack`, flagged any-thread): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L428-L453
* `dump_thread_stack` (watchdog, `hangdump` through `watchdog_dump_now`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/watchdog.cpp#L51-L91
* its automatic callers in the watchdog thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/watchdog.cpp#L95-L133
* registration of `stack` / `hangdump`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L651-L654

### How to reproduce
Timing-dependent; not reproduced on a running game yet.

1. Start a game instance with the console (`tools/lab.py` lane or `tools/spore.sh`).
2. While the game renders (for example in the creature stage), send `stack` repeatedly through the console (a loop of a few hundred calls).
3. At some call the console stops answering and the game freezes; `spore.sh kill` is needed.

A deterministic version needs the main thread to hold the CRT heap lock while `stack` runs (see **Test**).

### Expected
`stack`, `hangdump` and the watchdog dump never block while the target thread is suspended; the thread is always resumed within milliseconds.

## Root cause
**What happens**

1. `c_stack` opens the main thread and calls `SuspendThread(t)`.
2. While it is suspended, it calls `outf(...)` (appends to a `std::string`), `symbols_reverse(...)` (returns a new `std::string`) for every frame found, all of which allocate:
   ```cpp
   SuspendThread(t);
   ...
   outf(o, "eip=0x%08lX %s\n...", c.Eip, symbols_reverse(c.Eip).c_str(), ...);
   ...
   std::string r = symbols_reverse(v);
   outf(o, "  [esp+0x%04X] 0x%08X %s\n", i * 4, v, r.c_str());
   ...
   ResumeThread(t);
   ```
3. `dump_thread_stack` does the same: `SuspendThread(t)`, then `std::string out`, `out += b`, `describe()` (which calls `module_of` and `symbols_reverse`, both building strings), and only then `ResumeThread(t)`.
4. Allocation takes the heap lock. If the suspended thread was inside `malloc` / `free` / `operator new` (the main thread of Spore allocates constantly), the lock is owned by a thread that cannot run. The dumping thread waits on it forever and never reaches `ResumeThread`.
5. Result: the main thread stays suspended (game frozen, no frames, the window stops responding), and the console thread or watchdog thread is stuck too, so no further diagnostics are possible. The comment at the top of watchdog.cpp says it avoids the loader lock, but the heap lock has the same problem.

The automatic path matters most: the watchdog calls `dump_thread_stack` on the main thread after 15 s / 30 s without progress, and on the loader-lock owner after 10 s. A thread stuck inside `DllMain` often holds the heap lock as well.

### Confidence
CONFIRMED by reading the full path (suspend, allocate, resume order in both functions). Not verified on a running game; how often the main thread is inside the heap at a random instant is not measured.

## Fix
Fixed on fix/w1-sporekit. `thread_snapshot` (sporekit/watchdog.cpp) is the one place that suspends a
thread: it calls only `GetThreadContext`, `VirtualQuery` and copies the registers and up to 4096
dwords from esp into a caller-owned buffer (static, no allocation), then resumes. `stack`
(commands.cpp `c_stack`), `hangdump` and the watchdog (`dump_thread_stack`) format, resolve symbols
and log from the copy after the resume. The callback for the old order is a plain function pointer
(a `std::function` could allocate on the way to the suspend).

Switch: `kit.fix stackdump 0` formats while the thread is suspended (the old order).

Also: the 2026-10-22 note in docs/06-testing.md ("`stack` and `hangdump` suspend the main thread;
under Wine that hung the console for good") is very likely this bug.

## Verification
Test: `tools/tests/kit_freeze.py` cases `stack` and `hangdump` (suite `kit_freeze`). The console
command `kit.test.stackheap [stack|hangdump] 3000` makes the main thread lock the CRT heap
(`_get_heap_handle()`) and the process heap with `HeapLock` and sleep 3 s (as a thread suspended
inside malloc holds the heap), and then dumps its stack from the console thread.

FAIL (fixes off, `tools/tests/kit_freeze.py --old` on lane 3; manual run 2026-10-27 19:3x): `stack: no answer (25.0 s)`, `hangdump: no answer (25.0 s)`, frames stopped for good
(the game had to be killed), no crash report: the freeze of the report, reproduced.

PASS (fixes on): lab runs out/lab/20261027-184811 and out/lab/20261027-184904 (lane 3, all six cases PASS both times; `diag` passed in the second run, its one flaky check "a minidump is written next to it" failed in the first attempt and on main before, 2026-10-24/25). With the fix: `stack answered after 3001 ms (heaps held 3000 ms)`, `hangdump answered after
3003 ms`, frames go on.

### Related
`stack.scan` was added because suspending the main thread under Wine could hang the console (commands.cpp comment above `c_stack_scan`); this may be the same or a second cause.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #28.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: verified on today's main (88341cf: same code), fixed on fix/w1-sporekit with switch `kit.fix`; test kit_freeze FAIL with the fix off, PASS twice with it.
- 2026-10-28: released in 0.7.3.0.
