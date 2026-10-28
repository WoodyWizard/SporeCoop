---
id: BUG-0150
title: a deferred update file is never applied if its swap fails once (the pending list is deleted anyway)
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#129
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: 6e8e765
tests: tools/tests/win_unit.py + tests/win_tests.cpp (suite win_unit)
signature:
docs: 
related: #129
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/129 (full text, discussion).

When an in-game update cannot replace a file the game holds open (e.g. `Data\SporeCoop.package`), it leaves `<file>.new` and lists the file in `SporebinEP1\mods\sporecoop.pending`. At the next start the loader (and the updater's own `apply_pending`, called by the installer) tries the swap and then deletes the list whatever the result was. If the swap fails at that moment (a second game instance on the same game folder still holds the file, an antivirus scan locks it), the `.new` file is orphaned. Nothing retries it, the old file stays, and `sporecoop.version` already names the new version.

### Where
- Loader, swap result ignored for the list, list always deleted: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/loader/loader.c#L80-L92
- Updater's `apply_pending`, same pattern: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L307-L337
- Where files get deferred and the list is written: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L252-L285

### How to reproduce
1. Start two instances of the game on the same game folder (e.g. `spore.sh run -i 1` and `-i 2` on one lane).
2. Run an update in instance 1 (`coop.update apply` with a test release, or `update_check.py`): `Data\SporeCoop.package` is deferred.
3. Quit and restart instance 1 while instance 2 is still running.
4. `sporecoop.pending` is gone, `Data\SporeCoop.package.new` is still there, `Data\SporeCoop.package` is the old one. Quit both and start again: the old package stays.

### Expected
An entry whose swap did not happen stays in the list (or the list is rewritten with only the failed entries), so the next start applies it.

## Root cause
**What happens**

1. In-game update: `MoveFileExW(target, target.old)` fails with a sharing violation for a file the game has open, so the file is deferred: `.new` stays, its name goes into `sporecoop.pending`. All other files (coop.dll, sporekit.dll, the loader, `sporecoop.version`) are already the new version.
2. Next start: `apply_pending` runs `target -> .old`, `.new -> target`, and if that fails, `.old -> target`. Whether it worked or not, `DeleteFileW(list)` follows.
3. If the target was still in use (two instances share one game folder, which is how two instances per test lane run, and how a player tests co-op on one PC; or a scanner holds the file), the swap fails, and the entry is gone. The `.new` stays on disk forever (`cleanup_old` only removes `*.old`). The game keeps running the old `SporeCoop.package` with the new coop.dll until the next update or a reinstall.
4. The list is also read only up to 4096 bytes; entries beyond that are dropped with the same delete.

### Confidence
CONFIRMED by reading the code. A second reviewer checked it independently. Severity low (needs a file still in use at the next start, but then the mismatch is silent and permanent).

## Fix
Fixed on fix/w2-world (6e8e765). The pending swap is one shared function, `updater/pending.h`
(`sc_apply_pending`, plain C and Win32 only: the loader includes it, `upd::apply_pending` calls it).
It reads the whole list (up to 1 MB, was 4096 bytes), keeps every entry whose target could not be
moved aside or whose `.new` could not be moved in, rewrites the list with those and deletes it
only when it is empty. Old behaviour: `upd::g_old_fix = false` (the list is deleted).

The review suggested:

- In `loader.c` and `winupdate.cpp apply_pending`, collect the entries whose `.new` still exists after the attempt and write them back to the list; delete the list only when it is empty.
- Read the whole list (no 4096-byte limit), or cap the list when it is written.
- Optionally, when `.new` exists but is not listed, apply it too (it is only ever written by the updater).

## Verification
Windows-side unit test `win_unit` (tests/win_tests.cpp built with llvm-mingw, run under Wine in the lane's bottle by tools/tests/win_unit.py; real Win32 file calls, no game). `win_unit.py --old` switches the fixes off. Lane 3, 2026-10-28: FAIL with `--old` (21 failed checks), PASS with the fixes (lab runs 20261027-225936 and the smoke run of the branch). Case `test_pending`: the package is held open without sharing (another instance) while the list
is applied: with `--old` the list is deleted and the package never becomes the new one; with the
fix coop.dll is swapped, the package entry stays listed and is applied at the next call.

The planned test was:

Host-side or lane test of the updater:
- Prepare a game folder with `Data\SporeCoop.package.new` listed in `sporecoop.pending`, hold `Data\SporeCoop.package` open without share-delete from a helper process, and run `apply_pending` (the loader path through a game start).
- Release the handle and run `apply_pending` again.
- Assert the package now has the new content and the list is gone.
- FAILS on main (list deleted in the first run, the package never updated). PASSES with the fix.

### Related
- #67 (a pack listing the same file twice loses the original file on rollback)
- #38 (hot reload of coop.dll can crash while the update check thread still runs old code)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #129.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: entries whose swap failed stay in sporecoop.pending (win_unit).
- 2026-10-28: released in 0.7.3.0.
