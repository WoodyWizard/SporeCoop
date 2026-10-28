---
id: BUG-0056
title: a stack overflow ends the game without a crash report
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#34
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 1bebf57
tests: tools/tests/kit_crash.py stackoverflow (suite kit_crash)
signature: 0xC00000FD
docs: docs/02-modkit.md#modkit-crash-hard-cases
related: #34
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/34 (full text, discussion).

SporeKit's vectored exception handler also handles `EXCEPTION_STACK_OVERFLOW`, and it does heavy work (log formatting with about 4.3 KB of stack buffers, string and vector allocations) on the stack that just overflowed. That very likely overflows a second time with no guard page left, and Windows ends the process at once: no crash report, no minidump, no log line. The unhandled-exception filter that writes the report has the same problem. Severity: wrong behaviour (lost crash reports for one class of crashes).

### Where
* the handler: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/exceptions.cpp#L79-L161
* the log function it calls (stack buffers `msg[2048]` and `line[2300]`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/log.cpp#L64-L84
* the crash report filter, which also logs and formats on the faulting stack before handing off to its thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L622-L650

### How to reproduce
Not run yet (needs a game instance):

1. Add a test variant `crash.test stackoverflow` (a thread that recurses with a 4 KB local array until it overflows), or call any recursive function without end through `call`.
2. Run it on a lane and look at `SporebinEP1\mods\logs` for a `SporeCoop-crash-*.txt` / report zip and at the last lines of `sporekit.log`.
3. Expected without the fix: the process is gone, there is no crash file and no "exception 0xC00000FD" line.

### Expected
A stack overflow produces the same crash report as an access violation (at least the short `.txt` and the log line).

## Root cause
**What happens**

1. A thread overflows its stack (deep recursion in the game or a mod). Windows turns the guard page into a normal page and raises `STATUS_STACK_OVERFLOW` (0xC00000FD) on the same, nearly full stack.
2. `veh()` accepts this code (`code != EXCEPTION_STACK_OVERFLOW` is in the list of codes it handles), takes its mutex, copies the `CONTEXT` and `EXCEPTION_RECORD`, builds `ExcRec` with `std::string where = symbols_reverse(eip)`, `g_recs.push_back(r)`, then calls `logf(...)` up to three times. `logv` alone puts `char msg[2048]` and `char line[2300]` on the stack, plus `vsnprintf`'s own frame.
3. On 32-bit Windows only a little space is left below the old guard page after a stack overflow (by default about one page). Touching the stack below it hits memory with no guard page; the kernel cannot dispatch another exception on that stack and terminates the process.
4. If the handler did get through, `crash_filter` would again run `logf` and `snprintf(b[512])` on the same stack before it starts `crash_report_thread`.
5. The player sees the game vanish; logs\ has no crash file. The lab's "a crash after the checks is still a crash" check (CLAUDE.md section 3) also misses it when it relies on the crash report.

### Confidence
PLAUSIBLE. The heavy work on the faulting stack is confirmed by reading the full path; how much stack is left after the overflow (Windows and Wine) and whether the process really dies without a report is not verified at runtime.

## Fix
Fixed on fix/w3-sys (1bebf57). The exception logger only copies the record and the context of a
stack overflow into statics (no lock, allocation or log line) and returns. The unhandled filter only
signals a thread SporeKit made at boot (`so_report_thread`, waiting on an event) and waits for it;
that thread writes the short report, the log line, the full report, the archive and the minidump on
its own stack. The main thread gets 64 KB of stack guarantee at boot (`SetThreadStackGuarantee`).
`crash.test stackoverflow` (a thread recursing with 4 KB locals); `kit.fix stackov 0` = the old path.

## Verification
Test: `tools/tests/kit_crash.py --case stackoverflow` (suite `kit_crash`), one game on lane 3.
* FAIL with `kit.fix stackov 0` (`kit_crash.py --old`, manual run on lane 3, 2026-10-28 01:45): no
  crash report, no log line at all (the old handler died on the full stack). Under Wine 10 the
  overflowing worker thread is killed and the game goes on (the ticket's "the game ends" is the
  Windows behaviour; not checked on Windows).
* PASS with the fix: manual run 2026-10-28 01:46 (`crash-...-i5.txt` names 0xC00000FD, the log has
  "exception 0xC00000FD (stack overflow)") and lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #34.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (1bebf57); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
