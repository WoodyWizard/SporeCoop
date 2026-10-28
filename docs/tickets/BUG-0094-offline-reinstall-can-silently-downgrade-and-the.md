---
id: BUG-0094
title: offline "Reinstall" can silently downgrade, and the pack used is picked arbitrarily
status: fixed
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#72
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-installer
commits: eaf0a7c, 52be656
tests: net_tests test_pick_pack; win_unit (tests/win_tests.cpp test_setup_packs)
signature:
docs: docs/06-testing.md#testing-installer-w4, docs/INSTALL.md
related: #72
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/72 (full text, discussion).

When GitHub cannot be reached, SporeCoopSetup.exe installs the update pack found next to it without comparing its version with the SporeCoop already installed. A player with a newer SporeCoop who runs an older setup (for example from an old download folder) gets the older version installed, under a button labelled "Reinstall" and a message saying it is installed. If several `SporeCoop-*.pack` files lie next to the setup, the one used is simply the first the file system lists, which is not necessarily the newest (NTFS lists by name, so `SporeCoop-0.10.0.pack` comes before `SporeCoop-0.11.0.pack`). Severity: wrong behaviour (players end up on different versions, which co-op requires to match).

### Where
* `local_pack()`: the first `FindFirstFileW` match: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L182-L196
* `do_install()`: which pack is used, no comparison with the installed version: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L198-L233
* the button label, computed from the GitHub release only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L295-L302
* `installed_version()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L355-L369

### How to reproduce
1. Install SporeCoop 0.8.x into a game folder (`SporebinEP1/mods/sporecoop.version` says 0.8.x).
2. Put an older `SporeCoopSetup.exe` with its `SporeCoop-0.7.x.pack` into a folder, disconnect from the network (or pass `/api:` an address that does not answer).
3. Run it, click "Reinstall" (or run `SporeCoopSetup.exe /S /dir:<game folder> /api:http://127.0.0.1:9/`).
4. Observed by reading: 0.7.x is installed, exit code 0, `sporecoop.version` now says 0.7.x.
5. For the choice: put `SporeCoop-0.9.0.pack`, `SporeCoop-0.10.0.pack` and `SporeCoop-0.11.0.pack` next to the setup and stay offline. By name (`LC_ALL=C sort` gives the same order as NTFS here) `SporeCoop-0.10.0.pack` is listed first, so 0.10.0 is installed although 0.11.0 is the newest.

### Expected
* Among several local packs the setup uses the one with the highest version (`upd::compare_versions`), or asks.
* It never installs a version older than the installed one without saying so: the button reads "Install older version 0.7.x" and asks for confirmation; silent mode refuses with an error unless an explicit flag (for example `/downgrade`) is given.

## Root cause
**What happens**

1. `local_pack()` calls `FindFirstFileW(dir + "\\SporeCoop-*.pack")`, takes the first result and closes the search. The order is whatever the file system returns (alphabetical on NTFS, unspecified elsewhere), so the choice depends on the file names, not on the versions in them.
2. `do_install()` reads that pack. It goes to GitHub only when there is no usable local pack, or when the release check has succeeded and GitHub's version is newer:
   ```cpp
   bool have_local = !lp.empty() && upd::read_pack_file(lp, pack, err);
   if (!have_local || (g_rel_ok && upd::compare_versions(g_rel.version, pack.version) > 0)) {
   ```
3. Offline (`g_rel_ok` false) the local pack is installed whatever its version. `upd::installed_version(root)` is never consulted in `do_install`, so nothing stops installing 0.7.x over 0.8.x.
4. The button text comes from `v` (installed) and `g_rel` (GitHub): offline it shows "Reinstall" although a different, older version is about to be installed. Afterwards the status says "SporeCoop 0.7.x is installed", which is the only hint.
5. The in-game updater has a downgrade guard (mods/coop/autoupdate.cpp refuses a pack that is not newer); the setup has none.

### Confidence
CONFIRMED by reading the full path (`local_pack` -> `do_install` -> `apply_pack`, no version check against the installed one). Not run on Windows.

## Fix
Fixed on fix/w4-installer (eaf0a7c). The install steps moved to installer/install.cpp
(`inst::local_pack`, `inst::do_install`, `inst::do_uninstall`) so the Windows tests call them.

* `upd::pack_version_of` / `upd::pick_pack` (updater/pack.cpp, portable): of all
  `SporeCoop-<version>.pack` next to the setup the one with the highest version is used.
* `inst::do_install` compares the pack's version with `upd::installed_version(root)`; when it is
  older and `Options::downgrade` is not set it changes nothing and returns `Result::Older`. The window
  then asks ("SporeCoop X is installed. This setup has the older version Y ... Install Y anyway?",
  default No) and installs only on Yes; silent mode exits 1 with that message unless `/downgrade`.
* The button names what it installs: "Update to X", "Reinstall", or "Install older X", computed from
  the local pack and GitHub's release the same way do_install chooses.
* `/oldfix` (tests) brings back the first pack listed and no downgrade check.

## Verification
Tests:

* `net_tests` `test_pick_pack` (make test): `pick_pack({0.10.0, 0.11.0, 0.9.0})` is 0.11.0. FAIL with
  `net_tests --old` (first listed: 0.10.0), PASS with the fix.
* `win_unit` (tests/win_tests.cpp `test_setup_packs`, under Wine): three packs next to a fake setup
  folder, offline (`/api:` 127.0.0.1:9): local_pack picks 0.11.0 and installs it; then the 0.9.0 pack
  returns `Result::Older` and every file stays 0.11.0; with `downgrade` 0.9.0 is installed and the
  player's coop.ini stays. FAIL with `--old`: `local pack: ...SporeCoop-0.10.0.pack` (Wine listed it
  first) and `older pack: SporeCoop 0.9.0 is installed`, three CHECKs fail. PASS with the fix.

Runs: FAIL without the fix: `out/w4-fail/win_unit_old.txt` (manual run on lane 4, worktree fix/w4-installer); PASS: lab run `out/lab/20261028-030533` (win_unit), smoke `out/lab/20261028-030911` 14/14.

### Related
The in-game updater's downgrade guard in mods/coop/autoupdate.cpp (`the release pack is not newer`).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #72.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-installer (wave 4): newest pack, downgrade refused unless confirmed or /downgrade; host and Wine tests.
- 2026-10-28: released in 0.7.3.0.
