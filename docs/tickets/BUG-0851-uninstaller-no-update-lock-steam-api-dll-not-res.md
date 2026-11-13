---
id: BUG-0851
title: Uninstaller: no update lock, steam_api.dll not restored when missing, failures reported as removed, every .sym deleted
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 (installer/install.cpp do_uninstall, installer/uninstall.bat)
role: all
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: 0.7.13
branch: fix/w48-trust
commits: fcae15a5, 62a0bdd2, a2002fc2
tests: tests/win_tests.cpp (test_uninstall, test_uninstall_bat), tools/tests/tools_selftest.py symlist, make test
signature: 
docs: docs/INSTALL.md#8-update-and-uninstall, docs/06-testing.md#testing-trust-install
related: BUG-0139, BUG-0243
---

## Report
Rare cases where uninstalling leaves the game broken or half removed (inferred from code):
* steam_api.dll missing but steam_api_orig.dll present: nothing is restored and the game cannot start;
* a file that cannot be deleted (game running) is renamed to .old and "SporeCoop is removed" is still shown;
* an in-game update of a running instance can write files at the same time;
* every *.sym file in the symbols folder is deleted, also ones that are not ours;
* uninstall.bat copies steam_api_orig.dll over steam_api.dll without checking that the current one is our loader (a
  Steam-updated steam_api.dll would be replaced by an old copy) and leaves mods\coop.dll.

## Root cause
* Observed (code): installer/install.cpp:266 do_uninstall: no sc_update_trylock; restores only if
  `exists(orig) && upd::is_loader(sa)`; deletion failures fall back to MoveFileEx to .old and return true; deletes
  FindFirstFile(*.sym); ignores the sporecoop.pending marker after apply_pending.
* Observed: installer/uninstall.bat as described in the report.
* Confidence high for the code facts, low for frequency.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, install.cpp do_uninstall and uninstall.bat rewrite); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* do_uninstall: refuses a folder that is not a game (is_game / folder_problem); takes the updater lock with
  sc_update_trylock (updater/pending.h) held by a small RAII struct, else "Another update is in progress ...";
  after apply_pending refuses if SporebinEP1\mods\sporecoop.pending still exists.
* steam_api.dll: new upd::inspect_loader(path, bool& loader) (false if unreadable or empty; is_loader wraps it). If the
  current file is the loader or missing, the backup must be readable and not itself the loader, then MoveFileExW(orig,
  sa, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH); any failure stops with a message.
* Files: a remove_file lambda treats FILE/PATH_NOT_FOUND as success and any other DeleteFileW error as failure with a
  message; it deletes only the known symbol names (a new upd::kSymbolFiles list in winupdate.h, also used by
  own_legacy_backup), sporekit.dll, mods\coop.dll, Data\SporeCoop.package, mods\sporecoop.version.
* setup.cpp: Uninstall enabled also when the version file is missing but the loader or coop.dll is there.
* uninstall.bat: only calls `SporeCoopSetup.exe /S /uninstall "/dir:<game>"` (start /wait) and else tells the user
  to use the setup.
* setup.cpp also stops reading the real Steam libraries in test mode (SPORECOOP_SETUP_TEST_DELAY): a fixture must not
  resolve to the PC's real install.

## How to test
win_tests.exe cases: (a) fake game with steam_api_orig.dll and no steam_api.dll: after uninstall steam_api.dll is the
original; (b) coop.dll held open: uninstall reports failure, not success; (c) a foreign .sym in the symbols folder
survives; (d) uninstall while the update lock is held is refused. Each FAILs on main and PASSes with the fix.

## Fix
`inst::do_uninstall` (installer/install.cpp) refuses a folder that is not a game, takes the folder's update lock
without waiting (`upd::try_update_lock`; refused with "Another SporeCoop update is running" while an in-game update or
another setup holds it), runs the pending swaps under it and refuses while `mods\sporecoop.pending` is still there.
`steam_api.dll` comes back from `steam_api_orig.dll` when the current file is the loader **or missing**; it refuses
(nothing changed) when the backup is missing, unreadable or itself the loader (`upd::inspect_loader`). It deletes only
SporeCoop's files (`upd::kSymbolFiles`, no other `*.sym`), treats "not found" as done, and returns false with the
names of files it could not delete (no more "removed" after a rename to .old); the version file goes last.
`inst::has_sporecoop` keeps Uninstall enabled for a half removed install. `installer/uninstall.bat` runs
`SporeCoopSetup.exe /S /uninstall` when it lies next to it; otherwise it restores `steam_api.dll` only when the
current one is the loader (findstr on a tag line the loader now carries, loader/loader.c) or missing, refuses
without a valid `steam_api_orig.dll`, and deletes `mods\coop.dll` and the known symbol files too.
Not taken from the fork: the setup ignoring the real Steam libraries in test mode (test_setup_steam_ga needs them).
Switch: setup-only `inst::g_uninstall_fix` (setup `/olduninstall`, win_tests `--old-uninstall`, part of `--old`;
not a coop.fix switch: the setup is not coop.dll).

## Verification
Runs of `build/win_tests.exe` under Wine (soda-11.0-10) in a private prefix, `SPORE_WIN_UNIT_PREFIX=build/wineprefix
python3 tools/tests/win_unit.py` (no lane, no game; outputs kept in the worktree's out/w48-trust/):
- PASS: `win_unit.py`, ALL OK (out/w48-trust/win_unit_pass.txt): (a) restored, (b) "could not be removed ...
  SporebinEP1\mods\coop.dll", (c) the player's .sym kept, (d) "Another SporeCoop update is running", (e) refused,
  uninstall.bat 1. Steam's steam_api.dll kept, coop.dll removed, 3. with the setup: removed, log "OK".
- FAIL with the fix off: `win_unit.py --old-uninstall`, 13 FAILs, all in test_uninstall / test_uninstall_bat
  (out/w48-trust/win_unit_old-uninstall.txt): (a) steam_api.dll not restored, (b) "SporeCoop is removed" with
  coop.dll held, (c) mine.sym removed, (d) uninstalled under the lock, (e) removed everything without a backup,
  (f) Uninstall not offered, the old uninstall.bat (572c8e68) put the stale backup over Steam's file and left coop.dll.
- `tools_selftest.py symlist`: PASS; on the base code it has no `upd::kSymbolFiles` (FAIL by construction).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-trust): fixed as above; checked under Wine in a private prefix, FAIL with --old-uninstall.
- 2026-11-12: released in 0.7.13.
