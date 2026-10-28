---
id: BUG-0127
title: a crash from running out of memory can end the game before the minidump and the "last game crashed" notice are written
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#106
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 1bebf57
tests: tools/tests/kit_crash.py oom (suite kit_crash)
signature:
docs: docs/02-modkit.md#modkit-crash-hard-cases
related: #106
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/106 (full text, discussion).

Spore is a 32-bit game and many of its crashes come from a full address space. The crash report thread builds the report archive (up to 24 MB of buffers, log tails read in 8 MB blocks) before it writes the minidump and before anything can catch an allocation failure. In a process that crashed because memory ran out, a `std::bad_alloc` in that thread is not caught. It ends the whole game through `std::terminate` / `abort`. The minidump is never written, and depending on where it happens, `logs\last-problem.txt` is not written either, so the next start does not tell the player that a report is ready.

### Where
- The crash report thread: report text, then archive, then mark, then minidump, then archive again: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L602-L620
- The archive builder: whole files read into vectors, then copied into the entry list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L476-L560
- `read_file_tail` resizes a vector to the file size (up to the 8 MB / 24 MB caps): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/zip.cpp#L108-L127
- The filter waits up to 15 s for that thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L622-L650

### How to reproduce
1. In a test build, make the archive allocation fail: e.g. reserve most of the free address space with `VirtualAlloc` in a debug command (or `crash.test` after allocating until `new` throws), then trigger `crash.test thread`.
2. The game ends at once. `mods\logs` holds the short or full `crash-*.txt` but no `.dmp`, no zip, and `last-problem.txt` is missing. The next start shows no crash notice.

### Expected
Even when memory is exhausted, the minidump (or at least `last-problem.txt` pointing at the crash report) is written, and an allocation failure in the report code never ends the game by itself.

## Root cause
**What happens**

- The order (report, archive, mark first; minidump last) is intentional: the comment at diag.cpp L602-L603 calls the minidump the riskiest step. That is reasonable for lock trouble, but it puts the step with the largest allocations first.
- The build uses C++ exceptions (Makefile, no `-fno-exceptions`), and nothing in `crash_report_thread` or `diag_write_bundle_with` catches them. When an allocation of several MB fails in a fragmented or full 32-bit address space, `bad_alloc` leaves the thread function: `std::terminate`, `abort`, and the process ends right away.
- Result for the player: `crash-<time>.txt` (written first, small) may exist, but there is no `.dmp`, no `SporeCoop-report-*.zip`, and no `last-problem.txt`, so no "the last game crashed, a report to send is ready" message at the next start. Out-of-memory crashes are exactly the ones whose minidump matters most.

### Confidence
PLAUSIBLE, severity low. CONFIRMED by reading that no handler exists and that the large allocations come before the minidump; that they fail in a real out-of-memory crash is inferred. A second reviewer verified the reading.

## Fix
Fixed on fix/w3-sys (1bebf57). Every step of the crash report thread, the exit report and the hang
report runs in `report_step` (a `try` / `catch` per step that logs and goes on). The crash path marks
`last-problem.txt` with the crash `.txt` before the archive is built and re-marks it with the
archive when that exists; the minidump is written even when the archive failed. `crash.test oom`
makes every archive throw `std::bad_alloc` (as an allocation in a full address space would);
`kit.fix crashsafe 0` = the old order without catches. The archive is still built in memory (the
streaming writer the review suggested is not done: with the catches it is no longer fatal).

## Verification
Test: `tools/tests/kit_crash.py --case oom` (suite `kit_crash`), forced condition (the archive fails).
* FAIL with `kit.fix crashsafe 0` (manual run on lane 3, 2026-10-28 01:44): the game ends at once
  (`ExitProcess 3`, abort), no `.dmp`, no `last-problem.txt`.
* PASS with the fix: manual run 2026-10-28 01:47 (the `.dmp` written, `last-problem.txt` names the
  crash report, the next start shows "the last game crashed. A report to send is ready") and lab run
  out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #106.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (1bebf57); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
