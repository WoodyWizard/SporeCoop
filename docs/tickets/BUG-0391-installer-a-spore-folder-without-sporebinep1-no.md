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
fixed_in: unreleased
branch: fix/w31-installer
commits: 82d2314
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


## Root cause
Observed (setup source, win_tests with the setup built from main): `is_game` only knows a folder with
`SporebinEP1\SporeApp.exe`; every other folder got the same advice. Browse stepped up to the parent
only when the parent was a game folder, so a picked `SporeBin` stayed as it was. The auto-detection
looked only at registry keys and five fixed folders (no Steam library folders) and returned nothing
for a Spore without GA, so the field stayed empty.


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


## History
- 2026-11-01: opened.
- 2026-11-01: fix/w31-installer: the setup says "Galactic Adventures is not installed", steps up from
  SporeBin / SporebinEP1, looks in Steam's library folders; fixed (unreleased).
