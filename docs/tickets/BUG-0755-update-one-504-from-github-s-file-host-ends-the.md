---
id: BUG-0755
title: Update: one 504 from GitHub's file host ends the download (the updater never retries a 5xx or a broken connection)
status: fixed
kind: bug
stage: All
reported: 2026-11-08
source: Discord 2026-11-08 (Sterben, update window screenshot)
role: guest
versions: 0.7.10
fixed_in: 0.7.11
branch: fix/w46-update-retry
commits: d978f991
tests: win_unit (test_retry, test_setup_retry), update_flaky (update_check.py --flaky)
signature: "download failed: the server answered 504 for https://github.com/WoodyWizard/SporeCoop/releases/download/<tag>/SHA256SUMS.txt"
docs: docs/INSTALL.md (troubleshooting), docs/06-testing.md (win_tests)
related: BUG-0392
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->
The in-game update window of 0.7.10 said "download failed: the server answered 504 for
https://github.com/WoodyWizard/SporeCoop/releases/download/v0.7.10.1/SHA256SUMS.txt". The same files
downloaded fine a little later: a passing error of GitHub's file host.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->
Observed (code + test): updater/winupdate.cpp request() made one WinHTTP attempt; any status other
than 200, a failed connect or a broken read ended http_get / http_location, and with them
latest_release / download_pack for both coop.dll and SporeCoopSetup.exe. Also observed under Wine:
a body shorter than its Content-Length ends without a WinHTTP error (301 of 602 bytes read, avail 0),
so a cut-off pack reached the checksum check and failed as "does not match the release checksum".

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->
request() is now a retry loop around request_once(): transient failures (HTTP 5xx, 429, WinHTTP
timeout / cannot connect / connection error / name not resolved / invalid server response, a broken
read, a body shorter than Content-Length) are tried again, 3 attempts 1 s and 3 s apart; a 429/503
Retry-After of at most 10 s is waited for, a longer one is not (the API then falls back to the
release page, BUG-0392); no new attempt starts after 60 s. 4xx other than 429 is final. Each retry
is logged (upd::g_log: sporekit.log in game, the setup's /log: file); the final error ends with
"(after N attempts)"; a success clears the error of an earlier attempt. One code path for the API
call, SHA256SUMS.txt and the pack, in game and in the setup. The SHA-256 check and the
github.com-only asset rule (pack.cpp github_download) are unchanged.
Switch: `coop.fix update.retry off` (win_tests --old-retry, setup /oldretry).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- win_unit (test_retry, test_setup_retry: 504 x2 on SHA256SUMS.txt, closed connections, a cut-off
  body, 503, API 502, 429 Retry-After 1 / 3600, 404 asked once, 504 x9 -> "after 3 attempts",
  checksum still enforced; the real setup /S with 504 x2): PASS out/lab/20261108-100808,
  FAIL with SPORE_FIX_OFF=update.retry out/lab/20261108-100907 (11 failed checks; the setup logs the
  player's exact message).
- update_flaky (update_check.py --flaky, in game on a free lane: SHA256SUMS.txt 504 twice, the pack's
  first connection closed; the update installs, sporekit.log names 3 retries): PASS
  out/lab/20261108-100954, FAIL with SPORE_FIX_OFF=update.retry out/lab/20261108-101108
  ("update: failed ... error=the server answered 504 for .../SHA256SUMS.txt").

## History
- 2026-11-08: opened.
- 2026-11-08: fixed on fix/w46-update-retry (retry with backoff for every release download).
- 2026-11-08: released in 0.7.11.
