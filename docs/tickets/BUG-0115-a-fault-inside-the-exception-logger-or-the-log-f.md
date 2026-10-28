---
id: BUG-0115
title: a fault inside the exception logger or the log freezes the game for good
status: fixed
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#94
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-sporekit
commits: 5d46dbb, 4de4d0a, 5e4dc7f
tests: tools/tests/kit_freeze.py (suite kit_freeze)
signature: no crash report, the faulting thread blocked for good in veh / logv
docs: docs/02-modkit.md (Diagnostics: exception logger), docs/06-testing.md#testing-kit-freeze
related: #94
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/94 (full text, discussion).

SporeKit's vectored exception handler (`veh`) and its crash filter are not re-entrant. `veh` holds the exceptions `std::mutex` while it allocates memory, takes the loader lock (`GetModuleHandleExW`) and logs. `logv` holds the log `std::mutex` while it writes the file and changes the in-memory ring buffer (heap allocation and free). If a fault happens on a thread while it holds either mutex, `veh` runs again on the same thread and tries to take a mutex the thread already holds. With libc++ on llvm-mingw, `std::mutex` is an SRWLOCK, which cannot be taken twice, so the thread blocks forever. The game freezes with no crash report: the crash filter would need the log mutex too.

### Where
- `veh` takes the exceptions mutex, then does the risky work under it: `symbols_reverse` (allocates), `GetModuleHandleExW` (loader lock), `g_recs.push_back` (allocates), `logf` (log mutex): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/exceptions.cpp#L79-L162 (lock at L93; allocations L110, L120, L138; loader lock L113; logging L145-L159)
- `logv` holds the log mutex across `fputs`/`fflush` and the deque push/pop: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/log.cpp#L64-L85 (lock at L76)
- The crash filter logs before anything else: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L622-L628

### How to reproduce
1. A test-only command that takes the log mutex and then faults, for example a `crash.test log` variant that calls `logf` with a callback which dereferences null under the lock. Or a debug hook that faults inside `veh` after L93.
2. Expected with the bug: the game freezes (watchdog "no frames" after 30 s) and no `crash-*.txt` is written.
3. In real play the window is "a fault while one of the two locks is held", most likely on a heap that is already corrupt. The known issue "Creature chaos: a guest crashed with a corrupted heap" ends in a freeze inside the allocator. It is not shown that SporeKit's locks were involved there.

### Expected
A fault inside SporeKit's own fault logging is skipped (the handler returns `EXCEPTION_CONTINUE_SEARCH` at once). The crash filter still writes its short report.

## Root cause
**What happens**

The two racing sides are the same thread, re-entered by a fault.

- **Log mutex:** a thread is inside `logv`'s locked part and faults. On a corrupted heap that can happen in `g_ring.emplace_back` / `pop_front`, or in the CRT's `FILE` code. The first-chance fault calls `veh`, then `logf` (L145), then `logv` locks the log mutex again: deadlock. Logging is very frequent, so a corrupted process heap is likely to show up there.
- **Exceptions mutex:** a fault inside `veh` itself, for example in the allocation of `r.where` or in `g_recs.push_back`, calls `veh` again. It locks the exceptions mutex again: deadlock.
- **Across threads:** thread A is in `veh` holding the exceptions mutex and waits for the loader lock in `GetModuleHandleExW`. Thread B holds the loader lock (inside `LoadLibrary`, or a DllMain during thread start) and takes a first-chance fault. B's `veh` waits for the exceptions mutex. Neither thread ever runs again.

In every case the thread hangs instead of reaching the crash filter, so there is no crash report. If it is the main thread, the game stops drawing for good. The watchdog may then write a "no frames" dump, but that dump can freeze as well (#28).

### Confidence
- The mechanism (non-recursive SRWLOCK-based `std::mutex` taken again by the same thread from `veh`): CONFIRMED by reading.
- That a fault happens inside those regions in practice: PLAUSIBLE.

A second reviewer verified the mechanism. Severity: low.

## Fix
Fixed on fix/w1-sporekit.
* Per-thread flags in a TLS slot (`thread_flags`, sporekit/fixes.cpp; `TlsGetValue`, no allocation,
  last error kept): `SK_TF_IN_LOG` while a thread is inside the log's locked part (`logv`,
  `log_tail`, `log_init`), `SK_TF_IN_EXC` while it runs the exception logger or holds its lock
  (`exceptions_report`, `exceptions_last`, `exceptions_clear`).
* `veh` returns `EXCEPTION_CONTINUE_SEARCH` at once when `SK_TF_IN_EXC` is set (a fault inside
  itself or inside a holder of its lock); `logv` drops a line re-entered on a thread that holds
  the log's lock (the fault is still recorded in `exc`).
* `veh` copies the record under its lock only; the symbol / module name (`GetModuleHandleExW`
  takes the loader lock: the cross-thread case of the ticket) and the logging run after it let go.
* The crash filter writes the short crash report before its first log line.

Switch: `kit.fix reentry 0` (the flags are ignored: the old re-entry).

## Verification
Test: `tools/tests/kit_freeze.py` cases `logfault` and `excfault`: `kit.test.logfault` /
`kit.test.excfault` (main thread) touch a PAGE_NOACCESS page while holding the log's / the exception
logger's lock; SporeKit's handler (the first vectored handler) sees the fault, then a test handler
(the last one) makes the page readable and the instruction runs again.

FAIL (fixes off, `tools/tests/kit_freeze.py --old` on lane 3; manual run 2026-10-27 19:3x): `logfault: no answer (15.0 s)`, frames 74 -> 74; `excfault: no answer (15.0 s)`,
frames 81 -> 81. No crash report in either: the frozen game of the report.

PASS (fixes on): lab runs out/lab/20261027-184811 and out/lab/20261027-184904 (lane 3, all six cases PASS both times; `diag` passed in the second run, its one flaky check "a minidump is written next to it" failed in the first attempt and on main before, 2026-10-24/25). With the fix: "the thread goes on" at once, frames go on.

### Related
#28 (stack dump can freeze the game for good), #34 (a stack overflow ends the game without a crash report).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #94.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: verified on today's main (88341cf: same code), fixed on fix/w1-sporekit with switch `kit.fix`; test kit_freeze FAIL with the fix off, PASS twice with it.
- 2026-10-28: released in 0.7.3.0.
