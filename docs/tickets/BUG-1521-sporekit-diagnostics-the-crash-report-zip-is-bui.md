---
id: BUG-1521
title: SporeKit diagnostics: the crash report zip is built twice within one second under the same name (KIT-21)
status: open
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: 9b194edd
tests: tools/tests/kit_crash.py --case zipfail2 (kit_crash_zipfail2, twin kit_crash_zipfail2_off)
signature:
docs:
related: 
switch: kit.reportzip
---

## Report
External static audit of 0.7.15.5, item KIT-21 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-kit.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (sporekit/diag.cpp diag_write_bundle_with, crash_report_thread, zip.cpp): the archive is
named to the second (SporeCoop-report-<stamp>.zip) and a crash builds it twice (before and after the minidump). zip_write
opens with CREATE_ALWAYS and deletes the file on a write error: when both builds fell into the same second, the second
overwrote the first in place, and a failed or cut second write (the 15 s filter wait ending) left the archive
last-problem.txt names missing or truncated. Not seen in tickets.

## Fix
diag.cpp: a second archive of the same second gets its own name (-2, -3, ...), every archive is written to
`<name>.part` and moved over when complete (MoveFileEx), leftover `.part` files are pruned with the old archives. Test seam
`crash.test zipfail2` (a thread crash whose second archive takes the first one's second and fails while writing). Switch
`kit.fix reportzip 0` (0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: kit_crash_zipfail2, kit_crash_zipfail2_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run kit_crash_zipfail2 --lane N --retry 0 --no-build` (the archive last-problem.txt names is a whole zip).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run kit_crash_zipfail2_off --lane N --retry 0 --no-build` (`kit_crash.py --old`: the second write destroyed it).
* Regressions near it: kit_crash (oom, prune), report_shot.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (9b194edd); waiting for the night lab run.
