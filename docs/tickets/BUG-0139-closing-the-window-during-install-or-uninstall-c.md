---
id: BUG-0139
title: closing the window during Install or Uninstall can leave the game half installed
status: fixed
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#118
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-installer
commits: eaf0a7c, 52be656
tests: win_unit (tests/win_tests.cpp test_setup_close)
signature:
docs: docs/06-testing.md#testing-installer-w4
related: #118
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/118 (full text, discussion).

The setup runs Install / Uninstall on a worker thread (`job_thread`). While it runs, only the Install and Uninstall buttons are disabled. The Close button and the window's X stay usable. Closing the window ends the message loop and returns from `wWinMain`. The CRT then runs static destructors and calls `ExitProcess`, which kills the job thread wherever it is. If that is inside the file swap of `apply_pack`, or inside the `steam_api.dll` swap of the uninstall, the game folder is left half done. Re-running the setup repairs it, but until then Spore may not start.

### Where
- Buttons disabled while busy (Install / Uninstall only): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L303-L304
- The job thread, started and never waited for: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L327-L346
- The racing side on the UI thread: Close (`DestroyWindow`) and `WM_DESTROY` (`PostQuitMessage`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L428-L430 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L448-L450, then `wWinMain` returns: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L512-L518
- Install swap (each target is renamed to `.old`, then `.new` is renamed to the target): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L252-L271
- Uninstall swap of the loader (`steam_api.dll` to `.old`, then `steam_api_orig.dll` to `steam_api.dll`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L240-L247

### How to reproduce
1. Start SporeCoopSetup.exe and press Install (or Uninstall).
2. Press Close while the status says "Installing SporeCoop...".
3. The swap itself takes milliseconds, so hitting it by hand is luck. To widen the window for a test, add a test-only delay (for example an environment variable read by the setup) between the two moves in `apply_pack` / `do_uninstall`. Then close the window during it and check the game folder.

### Expected
Either the window cannot be closed while a job runs, or closing waits until the job is finished. The game folder is never left half updated.

## Root cause
**What happens**

- The job is killed between the two `MoveFileExW` calls for one file. The target is gone: it is `.old`, and `.new` is not in place yet. If that target is `SporebinEP1\steam_api.dll`, Spore does not start at all.
- The job is killed between two files. The folder holds a mix of new and old SporeCoop files, for example a new `sporekit.dll` with an old `coop.dll`.
- Uninstall killed between its two moves: there is no `steam_api.dll`, and Spore does not start.
- The CRT also runs static destructors (`g_rel`, `g_opt` strings) while the job thread may still read them. That can crash the setup on close.

Killing it during the download, or while the `.new` files are written, is harmless: nothing is replaced yet.

### Confidence
CONFIRMED by reading: the Close button and the X are enabled during a job, nothing waits for the job thread, and ExitProcess kills it. A second reviewer verified it. Severity: low, because the window is short and re-running the setup repairs it.

## Fix
Fixed on fix/w4-installer (eaf0a7c). While a job runs:

* `WM_CLOSE` (the X, Alt+F4) is refused: the status says "Please wait: SporeCoop's files are being
  changed. The window closes after that." and the setup beeps.
* The Close button is disabled and sends `WM_CLOSE` instead of `DestroyWindow`.
* The job thread's handle is kept (`g_job`); `wWinMain` waits for it before returning, so the CRT's
  `ExitProcess` never runs under a running job.

`/oldfix` (tests) brings back the old behaviour. For the test the setup reads
`SPORECOOP_SETUP_TEST_DELAY=<ms>`: `upd::apply_pack` and the uninstall wait that long between a
file's two moves (`upd::g_swap_delay_ms`).

## Verification
Test: `win_unit` (tests/win_tests.cpp `test_setup_close`, under Wine): 0.9.0 is installed into a fake
game folder; the real `build/SporeCoopSetup.exe` starts with a hidden window, `/pack:` a 0.11.0 pack,
offline, `SPORECOOP_SETUP_TEST_DELAY=1500`. The test clicks Install (WM_COMMAND to the button),
800 ms later sends WM_CLOSE and Close, waits for the job, closes, and checks every file.

* FAIL without the fix (`--old`, the setup with `/oldfix`): `closed during the job: yes`,
  `steam_api.dll` is missing (renamed to .sporecoop-old, the new one never placed).
* PASS with the fix: `closed during the job: no (refused)`, the window closes after the job, every
  file is 0.11.0.

Runs: FAIL without the fix: `out/w4-fail/win_unit_old.txt` (manual run on lane 4, worktree fix/w4-installer); PASS: lab run `out/lab/20261028-030533` (win_unit), smoke `out/lab/20261028-030911` 14/14.

### Related
#55 (Install before the GitHub check returns races on the release data), #67 (duplicate file in a pack loses the original on rollback), #72 (offline Reinstall can downgrade).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #118.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-installer (wave 4): no close while a job runs, the process waits for the job; GUI test under Wine.
- 2026-10-28: released in 0.7.3.0.
