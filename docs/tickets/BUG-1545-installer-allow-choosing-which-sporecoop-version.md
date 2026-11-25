---
id: BUG-1545
title: Installer: allow choosing which SporeCoop version to install in SporeCoopSetup.exe
status: fixed
kind: feature
stage: Installer
reported: 2026-11-24
source: owner request
role: any
versions: 0.7.99
fixed_in: 0.7.99.1
branch: rel/0.7.99.1
commits: c6531993
tests: win_unit, win_unit_verpick_off
signature:
docs: docs/INSTALL.md
related: BUG-0094, BUG-0392
---

## Report
`SporeCoopSetup.exe` only fetched `/releases/latest` and always installed the latest release (or a local `.pack`), leaving no built-in way to choose an older stable release when a newer major version (such as 0.8) is being tested.

## Root cause
The installer checked only `upd::latest_release()` (`releases/latest`) and had no UI control or CLI option to select another release tag.

## Fix
Added version selection across `updater/pack.{h,cpp}`, `updater/winupdate.{h,cpp}`, `installer/install.{h,cpp}`, and `installer/setup.cpp`:
- Added `upd::parse_releases`, `upd::releases_api_url`, `upd::web_releases_atom_url`, `upd::parse_releases_atom`, `upd::find_release`, and `upd::list_releases` (fetches up to 30 installable releases from GitHub's `/releases?per_page=30`, falling back to `releases.atom` on API 403/429 rate limits, then `latest_release`). Skips draft releases and marks prereleases while defaulting to the latest stable release (`inst::default_release`).
- Added a **Version to install** dropdown (`COMBOBOX`, `ID_VERSION`) to `SporeCoopSetup.exe` that updates the install button label dynamically and preserves the selected version across UAC elevation.
- Added `/version:<ver>` for silent mode (`SporeCoopSetup.exe /S /version:<ver>`, requiring `/downgrade` when downgrading over a newer installed version).
- Documented the feature in `docs/INSTALL.md` and added fuzz coverage in `tests/fuzz_decoders.cpp`, portable tests in `tests/net_tests.cpp`, and Win32/GUI tests in `tests/win_tests.cpp`.
Switch (setup-only): `/oldverpick` / `win_tests --old-verpick` (`inst::g_version_pick_fix = false`).

## Verification
- FAIL (`win_unit_verpick_off`, `SPORE_WIN_UNIT_ARGS=--old-verpick`): `out/lab/20261124-181144` (5 assertions failed: picking `0.7.17.1` installed `0.8.0`, dropdown had 0 items).
- PASS (`win_unit`): `out/lab/20261124-181144` (API list, 403 -> `releases.atom` fallback, `do_install` older version & downgrade protection, `/S /version:0.7.17.1`, and GUI combobox selection all passed).

## History
- 2026-11-24: opened from owner request.
- 2026-11-24: implemented in `c6531993` on `rel/0.7.99.1`, verified ON PASS + OFF FAIL in `20261124-181144`.
- 2026-11-24: released in 0.7.99.1.
