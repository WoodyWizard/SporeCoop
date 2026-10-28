---
id: BUG-0114
title: every game start deletes all *.old files in the game folders, also the player's own backups
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#93
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: 6e8e765
tests: tools/tests/win_unit.py + tests/win_tests.cpp (suite win_unit)
signature:
docs: 
related: #93
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/93 (full text, discussion).

`upd::cleanup_old` is meant to remove the files an update replaced last time. It deletes every file matching `*.old` in `SporebinEP1`, `SporebinEP1\mods`, `SporebinEP1\mods\sporekit\symbols` and `Data`, no matter who made it. It runs at every coop.dll start and in the installer. A player's own backup such as `SporeApp.exe.old` (a common name when patching the exe, e.g. a 4GB patch) or another mod's `.old` file is deleted silently.

### Where
- The pattern delete: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L339-L353
- Called at every coop.dll init: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L101
- Called by the installer after install and uninstall: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L230 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L262
- `apply_pack` also deletes an existing `<file>.old` before it renames the current file, for every file in the pack: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L252-L255

### How to reproduce
1. In the game folder, copy `SporebinEP1\SporeApp.exe` to `SporebinEP1\SporeApp.exe.old` (or put any `x.old` into `Data`).
2. Start Spore with SporeCoop installed (or run the installer's Install / Remove).
3. The `.old` file is gone.

### Expected
Only files that SporeCoop's updater renamed itself are deleted.

## Root cause
**What happens**

- `FindFirstFileW(dir + "\\*.old")` in four folders, `DeleteFileW` on every match. No list of what the updater itself renamed is kept.
- So on every game start with SporeCoop, a file like `SporebinEP1\SporeApp.exe.old`, `Data\<something>.package.old` or `mods\<othermod>.ini.old` is removed without a message.
- `apply_pack` does the same for the names of the pack's files (`steam_api.dll.old`, `sporekit.dll.old`, `mods\coop.dll.old`, `Data\SporeCoop.package.old`, the `.sym` files). That is a smaller risk, since those names belong to SporeCoop, but a player's own `steam_api.dll.old` backup is lost there too.

### Confidence
CONFIRMED by reading the code. A second reviewer checked it independently. Severity low (data loss of files the player made, in a narrow but real case).

## Fix
Fixed on fix/w2-world (6e8e765). An update's backup of a replaced file is
`<file>.sporecoop-old` (apply_pack, the pending swap in loader and updater, the installer's
uninstall). `cleanup_old` deletes only `*.sporecoop-old` in its four folders, plus the `.old`
backups that versions before the fix made of SporeCoop's own files (`upd::own_legacy_backup`:
sporekit.dll.old, mods\coop.dll.old, mods\sporecoop.version.old, symbols\*.sym.old,
Data\SporeCoop.package.old, and steam_api.dll.old only when it is the old loader). A player's
`SporeApp.exe.old` or another mod's `.old` file is never touched, and `apply_pack` no longer
deletes an existing `<file>.old`. Old behaviour: `upd::g_old_fix = false` (`win_tests --old`).
tools/tests/update_check.py looks for both suffixes.

The review suggested:

- Record the files `apply_pack` renamed to `.old` (e.g. in `mods\sporecoop.cleanup`, next to `sporecoop.pending`), and have `cleanup_old` delete only those, keeping entries whose delete fails (still loaded) for the next start.
- Alternatively, use a name only SporeCoop makes, such as `<file>.sporecoop-old`, and delete only that pattern.
- In `apply_pack`, do not delete an existing `<target>.old` that SporeCoop did not make: move it aside or use the unique suffix.

## Verification
Windows-side unit test `win_unit` (tests/win_tests.cpp built with llvm-mingw, run under Wine in the lane's bottle by tools/tests/win_unit.py; real Win32 file calls, no game). `win_unit.py --old` switches the fixes off. Lane 3, 2026-10-28: FAIL with `--old` (21 failed checks), PASS with the fixes (lab runs 20261027-225936 and the smoke run of the branch). Case `test_cleanup_old`: with `--old` SporeApp.exe.old, Data\keep.old, another mod's ini.old and a
non-loader steam_api.dll.old are deleted; with the fix they stay and SporeCoop's own backups go.

The planned test was:

A host-side test of the updater (like the existing `update_check.py` / net_tests for `pack.cpp`), with the old behaviour behind a switch:
- Put `SporebinEP1\SporeApp.exe.old` and `Data\keep.old` into a test game folder, apply a pack, then call `cleanup_old`.
- Assert both files still exist and the updater's own `.old` files are gone.
- FAILS on main (both player files deleted). PASSES with the fix.

### Related
- #67 (a pack listing the same file twice loses the original file on rollback)
- #58 (install.bat overwrites the player's coop.ini on a reinstall)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #93.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: own backup suffix, only SporeCoop's backups are deleted (win_unit).
- 2026-10-28: released in 0.7.3.0.
