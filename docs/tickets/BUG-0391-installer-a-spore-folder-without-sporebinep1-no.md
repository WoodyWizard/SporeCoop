---
id: BUG-0391
title: installer: a Spore folder without SporebinEP1 (no Galactic Adventures) is reported as 'choose the SPORE folder' instead of 'Galactic Adventures is not installed'
status: fixed
kind: bug
stage: Installer
reported: 2026-11-01
source: player report on Discord 2026-11-02 (Steam, base Spore only)
role: 
versions: 0.7.3.3 (setup)
fixed_in: 0.7.3.4
branch: fix/w31-installer
commits: 82d2314, 6608204
tests: win_unit, unit
signature:
docs: docs/INSTALL.md#9-troubleshooting, docs/06-testing.md#testing-installer-w31
related: BUG-0392
---

## Report
Steam player, SporeCoopSetup 0.7.3.3: chose `steamapps\common\Spore\SporeBin`. The Spore folder has
Data, installers, SporeBin, Support, installScript.vdf, runme.exe, steam_appid.txt and no SporebinEP1.
The setup said "Game: SporeApp.exe is not in this folder's SporebinEP1. Choose the SPORE folder." and
Install stayed disabled. Choosing the Spore folder itself would have said the same: Galactic
Adventures (the Steam DLC) is not installed, and nothing told the player so.


CORRECTION (2026-11-01, fix/w31-installer): the player most likely HAS Galactic Adventures. On a
real Steam install (this machine) GA is its own app 24720 in a separate folder,
`steamapps\common\spore 24720` (appmanifest_24720.acf "installdir"), the only one with SporebinEP1
(`SporeApp.exe` 25 066 744 bytes); `steamapps\common\Spore` (app 17390) has only Data, SporeBin,
Support, installers, runme.exe: exactly the screenshot.


## Root cause
Observed (setup source, win_tests with the setup built from main): `is_game` only knows a folder with
`SporebinEP1\SporeApp.exe`; every other folder got the same advice. Browse stepped up to the parent
only when the parent was a game folder, so a picked `SporeBin` stayed as it was. The auto-detection
looked only at registry keys and five fixed folders (no Steam library folders) and returned nothing
for a Spore without GA, so the field stayed empty.


CORRECTION (2026-11-01): the first fix (82d2314) told that player "Galactic Adventures is not
installed" too: it never looked at the sibling GA folder.


## Fix
installer/install.cpp: `spore_without_ga` (a `SporeBin` and no `SporebinEP1\SporeApp.exe`),
`folder_problem` ("Galactic Adventures is not installed: SporeCoop needs Spore + Galactic Adventures
(on Steam: the Spore Galactic Adventures DLC). This Spore folder has no SporebinEP1."; used by the
window's "Game:" line and by do_install / silent mode), `game_root_of` (a picked or typed `SporeBin` /
`SporebinEP1` goes to its parent; used by Browse, the typed field, `/dir:`), `steam_libraries`
(libraryfolders.vdf, both formats) and `pick_game` (a game first, else a Spore without GA).
setup.cpp: find_game also tries `<library>\steamapps\common\Spore` of every Steam library; the "Game:"
line has two lines of room. Switch: `inst::g_ga_fix` (not a lab switch: setup only; setup `/oldga`,
win_tests `--old-ga`, lab `SPORE_WIN_UNIT_ARGS=--old-ga`).


Second step (6608204): `inst::resolve_root` switches from base Spore (its SporeBin, or a chosen
steamapps\common) to the GA folder next to it (appmanifest_24720.acf installdir, else "Spore 24720",
any case) or in another Steam library, and the window / silent log say "Galactic Adventures is
installed in ...: using that folder." find_game tries every Steam library's GA folders first
(registry SteamPath / InstallPath, the default folder, libraryfolders.vdf). "Galactic Adventures is
not installed" only when none exists, naming "Spore 24720". Switch `inst::g_ga_dir_fix` (setup
`/oldgadir`, win_tests `--old-gadir`, lab `SPORE_WIN_UNIT_ARGS=--old-gadir`).


## Verification
Tests: tests/win_tests.cpp `test_ga_folder` (unit) and `test_setup_ga_window` (the real
SporeCoopSetup.exe, hidden window, `/dir:<spore>\SporeBin`, reads the window's texts).
- PASS: lab 20261101-105507 (win_unit, lane 4, ALL OK).
- FAIL with the fix off: lab 20261101-105627 (`SPORE_WIN_UNIT_ARGS=--old-ga`: 6 checks, among them
  the window's "Game:" line and the folder field).
- FAIL on main's code: `win_unit.py --setup build/main-setup/SporeCoopSetup.exe` (setup built from
  `git archive main` 563cf27): the window showed `...\ga_window\SporeBin` and "Game: SporeApp.exe is not
  in this folder's SporebinEP1. Choose the SPORE folder." (the player's text), 2 FAILs in
  test_setup_ga_window.


Second step (Steam GA folder): tests `test_steam_ga_folder`, `test_setup_steam_ga` (window and `/S`
with `/dir:<lib>\steamapps\common\Spore\SporeBin`), `test_real_steam` (read only, ~/.local/share/Steam
through Wine's Z:: libraryfolders.vdf, installdir 'spore 24720', base Spore\SporeBin resolves to it).
- PASS: lab 20261101-112317 (win_unit, lane 4).
- FAIL with the switch off: lab 20261101-112357 (`SPORE_WIN_UNIT_ARGS=--old-gadir`: 14 checks).
- FAIL on main c891445 (the first fix): `win_unit.py --setup build/main-setup/SporeCoopSetup.exe`
  (built from `git archive c891445`): window "Galactic Adventures is not installed ...", Install
  disabled, `/S` exit 1; 5 FAILs in test_setup_steam_ga.


## History
- 2026-11-01: opened.
- 2026-11-01: fix/w31-installer: the setup says "Galactic Adventures is not installed", steps up from
  SporeBin / SporebinEP1, looks in Steam's library folders; fixed (unreleased).
- 2026-11-01: correction: Steam installs GA into its own folder "Spore 24720"; the setup now uses it
  (6608204).
- 2026-11-02: released in 0.7.3.4.
