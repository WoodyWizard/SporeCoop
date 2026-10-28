---
id: BUG-0077
title: clicking Install before the GitHub check returns races on the release data
status: fixed
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#55
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-installer
commits: eaf0a7c
tests: make test-tsan (tests/release_tsan.cpp, in make test)
signature:
docs: docs/06-testing.md#testing-installer-w4
related: #55
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/55 (full text, discussion).

SporeCoopSetup.exe starts a background check of the newest GitHub release when its window opens. If the player clicks Install (or Update / Reinstall) before that check has finished, the install thread fetches the release itself and writes the same global `g_rel` (strings) while the check thread may assign it too, and while the UI thread reads it in `refresh()`. Concurrent writes to a `std::string` without a lock are undefined behaviour and can corrupt the heap: the setup can crash or show garbage. Severity: minor (narrow timing window, possible crash).

### Where
* the check thread, writes `g_rel`, `g_rel_ok`, `g_rel_err` without a lock: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L307-L318
* started from `WM_CREATE`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L394-L395
* the install thread, `upd::latest_release(g_opt.api, g_rel, err)` when `g_rel_ok` is still false: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L198-L225
* the UI thread reading `g_rel.version` (on `WM_INFO`, on every keystroke in the folder box, on `WM_DONE`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L275-L305
* `start_job` starting the install thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L335-L346

### How to reproduce
Timing-dependent, not reproduced yet:

1. Make the release check slow: run `SporeCoopSetup.exe /api:http://127.0.0.1:<port>/latest` against a small local server that answers the same JSON after a delay of about 3 s (or use a throttled network).
2. Click Install within that delay; the install thread fetches the release at the same moment as the check thread.
3. Under a debugger with page heap (or a build with the address sanitizer for mingw, where available) the race shows as a heap error; without one it may crash rarely.

### Expected
The release data is read and written under one lock, or the install job never touches the shared copy; clicking Install at any moment is safe.

## Root cause
**What happens**

1. The window opens; `check_thread` starts `upd::latest_release(g_opt.api, r, err)` (an HTTPS call, from a few hundred ms up to the WinHTTP timeouts of 10 s + 10 s + 15 s + 30 s on a slow network).
2. The player clicks Install. `start_job` starts `job_thread` -> `do_install`. No local pack is used (or it is older), and `g_rel_ok` is still false, so `do_install` calls `upd::latest_release(g_opt.api, g_rel, err)`, which ends in `parse_release(json, g_rel)`: `out = Release{}` and then assignments to `tag`, `version`, `page_url`, `pack_url`, `sums_url`, `setup_url`.
3. Meanwhile `check_thread` finishes and runs `g_rel = r; g_rel_ok = ok; g_rel_err = ...;` and posts `WM_INFO`, whose handler calls `refresh()` on the UI thread, which builds strings from `g_rel.version` and calls `compare_versions(g_rel.version, v)`.
4. Two threads assigning the same `std::string` members, or one assigning while another copies, can free a buffer the other one is still using: heap corruption, a crash in the setup, or a wrong version shown. `do_install` then also uses `g_rel` for `download_pack(g_rel, ...)` while `check_thread` may still overwrite it.
5. `g_rel_ok` is a plain `bool` and `g_busy` a `volatile bool`: no ordering guarantee either.

### Confidence
CONFIRMED by reading the full path (both threads write `g_rel` without synchronisation, the UI thread reads it). PLAUSIBLE that it crashes in practice: the window is the time the release check needs.

## Fix
Fixed on fix/w4-installer (eaf0a7c). The release state is an `upd::ReleaseBox` (updater/pack.h): `get()`
returns a copy and `set()` replaces it, both under one `std::mutex`. The check thread fills it,
`refresh()` reads a copy, and the install job (`inst::do_install`, installer/install.cpp) works on
its own copy: when the check has no result yet it asks GitHub into that copy and then stores it
in the box. Nothing writes a shared `std::string` without the lock any more. `g_busy` is a
`std::atomic<bool>`. `upd::g_release_lock_fix = false` brings back the unlocked access (tests).

## Verification
Test: ThreadSanitizer, `make test-tsan` (part of `make test`): `tests/release_tsan.cpp` runs two
writer threads (20 000 `set()` each with different releases) and a reader (20 000 `get()`) on one
`ReleaseBox`.

* FAIL without the fix: `build/host/release_tsan --old` (no lock, as the old global):
  `WARNING: ThreadSanitizer: data race ... in upd::Release::operator=`, exit code 66.
* PASS with the fix: `ALL OK`, no report (make test, several runs).

The race in the window itself is not forced in a GUI test: the box is the only path the setup has
to the release data now (setup.cpp has no other global for it), which the review asked for. The
GUI paths around it run in `win_unit` (test_setup_close: the real SporeCoopSetup.exe, check thread
and install job at the same time).

Runs: FAIL without the fix: `out/w4-fail/(TSAN, make test-tsan: no run dir)` (manual run on lane 4, worktree fix/w4-installer); PASS: lab run `out/lab/20261028-030533` (unit in smoke), smoke `out/lab/20261028-030911` 14/14.

### Related
None known.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #55.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-installer (wave 4): ReleaseBox under a mutex, the job uses its own copy; TSAN test.
- 2026-10-28: released in 0.7.3.0.
