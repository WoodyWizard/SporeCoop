---
id: BUG-0080
title: install.bat overwrites the player's coop.ini (name, update opt-out) on a reinstall
status: fixed
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#58
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-installer
commits: 52be656
tests: win_unit (tests/win_tests.cpp test_install_bat)
signature:
docs: docs/06-testing.md#testing-installer-w4, docs/INSTALL.md
related: #58
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/58 (full text, discussion).

The documented manual update ("extract the new zip into `SporebinEP1` over the old files and run `install.bat` again") replaces the player's `mods\coop.ini` and `mods\sporekit.ini` with the defaults. That loses the player name, the last joined address, the host port, and the `[update] check=0` opt-out from the update check. A player who turned the GitHub check off gets it turned back on without being told. The in-game update and `spore.sh deploy` keep these files; only the zip path resets them.

### Where
- `install.bat` (written by `package.sh`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L140-L141
- The zip ships the default inis inside `SporeCoop\mods`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L112
- The update pack keeps them on purpose (`ONLY_IF_MISSING`, "the player's settings stay"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/mkpack.py#L25-L26
- `spore.sh deploy` also copies an ini only when missing: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L74-L77
- Settings the mod writes into `coop.ini`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L267-L319 and the opt-out read in https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L97
- The documented paths: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/INSTALL.md#L216-L224

### How to reproduce
1. Install a release zip with `install.bat`, start the game, set a name in the Co-op window (or add `[update]` / `check=0` to `mods\coop.ini` as INSTALL.md says).
2. Extract the same or a newer zip over `SporebinEP1` and run `install.bat` again.
3. `mods\coop.ini` is the default file again: `name=Player`, no `[update]` section; the next start asks GitHub for updates.

(Read from the script; not run on Windows.)

### Expected
A reinstall from the zip keeps existing `.ini` files, like the in-game update and the setup do.

## Root cause
**What happens**

```bat
if not exist mods mkdir mods
xcopy /e /i /y /q SporeCoop\mods mods >nul
```

`/y` overwrites existing files without asking, so `SporeCoop\mods\coop.ini` (defaults: `name=Player`, no `[update]` section) replaces the player's file. The UI saves `coop/name`, `ui/last_address` and `ui/host_port` into that same file (`save_str` -> `config_set_str` -> `WritePrivateProfileStringW` on `mods\coop.ini`), and INSTALL.md section 8 tells players to put `check=0` there.

### Confidence
CONFIRMED by reading.

## Fix
Fixed on fix/w4-installer (52be656). install.bat and uninstall.bat are files now
(`installer/install.bat`, `installer/uninstall.bat`; tools/package.sh copies them). install.bat copies
`SporeCoop\mods` with `xcopy /exclude:SporeCoop\ini.exclude` (the file holds `.ini`, shipped in
the zip) and then each `SporeCoop\mods\*.ini` only when `mods\<name>` is missing:

```bat
xcopy /e /i /y /q /exclude:SporeCoop\ini.exclude SporeCoop\mods mods >nul
for %%f in (SporeCoop\mods\*.ini) do if not exist "mods\%%~nxf" copy /y "%%f" "mods\%%~nxf" >nul
```

Under Wine `xcopy /exclude:` with a missing exclude file copies nothing at all: the zip always has
it next to the mods folder.

## Verification
Test: `win_unit` (tests/win_tests.cpp `test_install_bat`, under Wine in the lane's bottle): a fake
`SporebinEP1` with a dummy SporeApp.exe and a `SporeCoop` folder; install.bat runs (`cmd /c
install.bat < NUL`), `mods\coop.ini` is changed to a name and `[update] check=0`,
`mods\sporekit.ini` is deleted, the dlls and symbols in `SporeCoop` change, install.bat runs again.

* FAIL without the fix (`win_unit.py --old`: the install.bat of 7874654, from tools/package.sh):
  `coop.ini after the reinstall: replaced`, `FAIL ... get(bin + L"\\mods\\coop.ini") == mine`
  (lane 4, see the run below).
* PASS with the fix: `coop.ini after the reinstall: kept`, the missing sporekit.ini is back, the new
  dlls and symbols are in place; `ALL OK`.

Runs: FAIL without the fix: `out/w4-fail/win_unit_old.txt` (manual run on lane 4, worktree fix/w4-installer); PASS: lab run `out/lab/20261028-030533` (win_unit), smoke `out/lab/20261028-030911` 14/14.

### Related
`tools/mkpack.py` comment "the player's settings stay"; docs/INSTALL.md section 8.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #58.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-installer (wave 4): install.bat keeps the .ini files; Wine test test_install_bat.
- 2026-10-28: released in 0.7.3.0.
