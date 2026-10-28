---
id: BUG-0137
title: crash dumps, reports and report zips (up to 24 MB each) pile up in mods\logs and are never removed
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#116
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 1bebf57
tests: tools/tests/kit_crash.py prune (suite kit_crash)
signature:
docs: docs/02-modkit.md#modkit-crash-hard-cases
related: #116
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/116 (full text, discussion).

Every crash, hang, game end after a fault, and every "Report a problem" writes new files into `SporebinEP1\mods\logs`: `crash-<time>.txt` and `.dmp`, `report-<time>.txt`, and a `SporeCoop-report-<time>.zip` of up to 24 MB that contains copies of the newest three dumps again. None of these are ever deleted. Only the logs of earlier runs in `logs\previous` are rotated (the newest 10 per log name are kept, log.cpp L21 and L51). A player with a recurring crash collects tens of MB per start in the game folder.

### Where
- Crash / report text and minidump written per event: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L333-L406
- Archive limit 24 MB, written as a new file per event: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L409-L413 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L547-L556
- The only deletes in diag.cpp: a failed dump, `last-problem.txt`, and the first zip of a crash once the second exists: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L313-L316, https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L467 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L611-L616
- For comparison, the rotation that does exist for previous logs: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/log.cpp#L18-L53

### How to reproduce
1. Run `crash.test thread` (or `report`) five times, restarting the game in between.
2. `mods\logs` now holds five `crash-*.txt`, five `.dmp` files and five `SporeCoop-report-*.zip`, none removed.

### Expected
Only the newest few of each kind stay (e.g. 5 crash reports with their dumps, 5 other reports, 3 report zips), like `logs\previous` keeps 10 logs per name.

## Root cause
**What happens**

- Per crash: one `.txt`, one `.dmp`, one zip (up to 24 MB, holding up to three dumps). Per hang or fault-then-exit: one `.txt` plus one zip. Per report button or console `report`: one `.txt` plus one zip.
- `files_in(..., L"crash-*.txt")` and `newest(..., 3)` only decide what goes into the next zip. They do not remove older files.
- A crash on every start (a broken save, a bad overlay, a mod conflict) therefore adds roughly 24 MB or more per start. Under Program Files or on a small system drive this fills the disk over time, and the folder becomes hard for a player to search for "the newest report".

### Confidence
CONFIRMED by reading the code. A second reviewer checked it independently. Severity low (disk use, no data loss of the player's own files).

## Fix
Fixed on fix/w3-sys (1bebf57). `diag_prune_logs` runs after every archive and at boot: the newest 5
`crash-*.txt`, 5 `crash-*.dmp`, 5 `report-*.txt` and 3 `SporeCoop-report-*.zip` stay (names sort by
time); files named in `last-problem.txt` or the previous run's mark are never removed.
`kit.fix logprune 0` = nothing removed.

## Verification
Test: `tools/tests/kit_crash.py --case prune` (12 planted old files of each kind, then `report test`).
* FAIL with `kit.fix logprune 0` (manual run on lane 3, 2026-10-28 01:45): 12 / 12 / 13 / 13 left.
* PASS with the fix: manual run 2026-10-28 01:47 (5 / 5 / 5 / 4, the marked archive kept) and lab
  run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #116.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (1bebf57); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
