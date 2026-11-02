---
id: BUG-0392
title: installer/updater: GitHub API answers 403 (unauthenticated rate limit, shared IP / VPN) and the setup cannot install; fall back to the releases/latest web redirect and the release asset URL
status: fixed
kind: bug
stage: Installer
reported: 2026-11-01
source: player report on Discord 2026-11-02 (setup 0.7.3.3: 'GitHub not reachable (the server answered 403 for ...)')
role: 
versions: 0.7.3.3 (setup and in-game update)
fixed_in: 0.7.3.4
branch: fix/w31-installer
commits: 82d2314
tests: win_unit, update_api403, unit
signature: the server answered 403 for https://api.github.com/repos/WoodyWizard/SporeCoop/releases/latest
docs: docs/INSTALL.md#9-troubleshooting, docs/06-testing.md#testing-installer-w31
related: BUG-0391
---

## Report
Steam player, SporeCoopSetup 0.7.3.3: "Newest release: GitHub not reachable (the server answered 403
for ...)"; with no pack next to the setup it could not install at all. The in-game update check fails
the same way (silently: it stays idle, so no update is ever offered).


## Root cause
Observed: api.github.com allows 60 unauthenticated requests an hour per IP address and answers 403
("API rate limit exceeded", `x-ratelimit-remaining: 0`) or 429 after that; shared IPs and VPNs hit it.
On 2026-11-01 this machine got exactly that 403 for the WinHTTP request, and the setup built from main
(563cf27) failed with the player's message in silent mode against the real GitHub. `upd::latest_release`
only knew the API; the asset downloads (github.com/.../releases/download/...) were never the problem.


## Fix
updater: when the API fails, `latest_release` asks `web_latest_url(api)`
(https://github.com/WoodyWizard/SporeCoop/releases/latest; the local test server keeps its origin)
without following the redirect (`http_location`: WINHTTP_DISABLE_REDIRECTS, Location header; a 200
gives WINHTTP_OPTION_URL), takes the tag from `.../releases/tag/<tag>` (`release_from_redirect`: plain
tag characters only, a version starting with a digit) and builds the asset URLs on github.com
(`.../releases/download/<tag>/SporeCoop-<version>.pack`, `SHA256SUMS.txt`, `SporeCoopSetup.exe`), never
on the redirect's host. Integrity: the pack is still checked against the same release's
SHA256SUMS.txt, and a release found this way must hold a pack of the tag's version (`from_web`).
Used by the setup and the in-game check (autoupdate logs "from the release page", `coop.update` shows
`via=page`). When both fail, the setup's message (and a message box, and the status line once the
check failed without a local pack) says how to install by hand: release zip into SporebinEP1 +
install.bat. Switch `update.web_fallback` (setup `/oldweb`, win_tests `--old-web`).


## Verification
Tests: tests/win_tests.cpp `test_web_fallback` (in-process fake GitHub: 403 / 429 on the API path,
302 absolute / relative on the release page, assets; bad checksum and wrong pack version refused;
both refusing -> the manual way) and `test_setup_api403` (the real setup `/S`); tools/tests/
update_check.py `--api403` (suite update_api403, in game); net_tests (URL parsing).
- PASS: lab 20261101-105507 (win_unit, lane 4).
- FAIL with the fix off: lab 20261101-105547 (`SPORE_FIX_OFF=update.web_fallback`, win_unit: 8 checks).
- FAIL on main's code: `win_unit.py --setup build/main-setup/SporeCoopSetup.exe`: test_setup_api403
  "exit 1, FAILED: cannot get SporeCoop from GitHub: the server answered 403 for .../repos/WoodyWizard/
  SporeCoop/releases/latest".
- In game, lane 4, by hand (not lab runs): `update_check.py --lane 4 --api403` PASS (game offers and
  installs 0.7.3.4 `via=page`, out/w31/update_api403_fix.txt); with `SPORE_FIX_OFF=update.web_fallback`
  FAIL ("the game finds SporeCoop 0.7.3.4", "Update button", out/w31/update_api403_off.txt).
- Real GitHub, lane 4's bottle, `/S` into a fake game folder: main's setup "FAILED: cannot get
  SporeCoop from GitHub: the server answered 403", the fixed setup "OK: SporeCoop 0.7.3.3 is installed"
  through the release page.


## History
- 2026-11-01: opened.
- 2026-11-01: fix/w31-installer: release page fallback in the setup and the in-game check, manual
  install message; fixed (unreleased).
- 2026-11-02: released in 0.7.3.4.
