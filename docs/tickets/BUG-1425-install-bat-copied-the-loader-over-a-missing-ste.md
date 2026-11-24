---
id: BUG-1425
title: install.bat copied the loader over a missing steam_api_orig.dll and said installed after failed copies (KIT-25)
status: fixed
kind: bug
stage: Installer
reported: 2026-11-22
source: audit
role: both
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-net
commits: 238644aa
tests: win_unit (test_install_bat_backup), win_unit_kit25_off
signature:
docs:
related: BUG-0080, BUG-0784, BUG-0851
---

## Report
KIT-25 of the external static audit of 0.7.15.5 (verified row in out/research/ext-audit-verify-kit.md: REAL, install
broken, no save damage). Unchanged on rel/0.7.16 (installer/install.bat at 5376d388). A reinstall with install.bat
after steam_api_orig.dll was lost (deleted, quarantined by an antivirus, BUG-0784) copied SporeCoop's loader over the
backup's place: the loader then forwarded to itself and the game did not start until Steam or GOG restored the file.
Every copy's result was hidden (`>nul`, no errorlevel): "SporeCoop installed." after an access-denied copy (Program
Files without admin rights, a running game).

## Root cause
Observed under Wine (cmd.exe, win_tests.exe in a private prefix, 2026-11-22, the install.bat of 5376d388): after
an install and a deleted steam_api_orig.dll, a second run left steam_api_orig.dll = the loader (its marker line
`sporekit-loader (SporeCoop)`); a loader as the backup stayed a loader. `if not exist steam_api_orig.dll copy ...` had
no loader check (the setup has one: winupdate.cpp `is_loader`; uninstall.bat checks `findstr sporekit-loader`).

## Fix
install.bat: a steam_api_orig.dll that is the loader counts as missing; the backup is made only from a steam_api.dll
that is not the loader, else the install stops with "The game's own steam_api.dll is missing ... Verify the game files
in Steam or GOG Galaxy" (exit 1). Every copy / xcopy is checked (`if errorlevel 1`, `|| set SCFAIL=1` in the .ini
loop): a failed one stops with "SporeCoop could not copy its files ... close Spore, run as administrator, or use
SporeCoopSetup.exe" (exit 1); a whole install ends with exit 0. No runtime switch (a batch file); the twin runs the
install.bat of 5376d388 (win_unit.py --old-kit25).

## Verification
Open, waiting for the lab run. Pre-checked without the lab (win_tests.exe in a private headless Wine prefix,
soda-11.0-10, no game, 2026-11-22): the new install.bat ALL OK (1. backup lost: exit 1, no backup made; 2. a loader
backup with the game's file back: the game's file becomes the backup; 3. read-only steam_api.dll: exit 1); the
install.bat of 5376d388 FAILED (4) in test_install_bat_backup (the loader as steam_api_orig.dll in 1 and 2).
Lab: `tools/lab.py run win_unit win_unit_kit25_off`: win_unit must PASS, win_unit_kit25_off
(SPORE_WIN_UNIT_ARGS=--old-kit25) must FAIL.

- 2026-11-23 (0.7.16.1 final check): win_unit PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened.

- 2026-11-22: renumbered from BUG-9310 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9310.
- 2026-11-23: released in 0.7.16.1.
