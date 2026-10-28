---
id: BUG-0060
title: hot reload of coop.dll can crash while the update check thread or a queued task still runs old code
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#38
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: 2bb6835, 066df15, 5bbb466
tests: tools/tests/coop_reload_update.py (suite reload_update)
signature:
docs:
related: #38
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/38 (full text, discussion).

Every load of coop.dll starts a background update-check thread (WinHTTP to the GitHub API, timeouts up to 30 s). `on_unload` neither waits for that thread nor for a running update download. A `mod.reload coop` (the dev loop `tools/lab.py reload`) shortly after a load frees the DLL while the thread is still blocked in WinHTTP. When the thread returns, it jumps into unmapped memory or into the new copy at the same address. Outside-frame tasks queued by the old DLL have the same exposure. Severity: crash (developers and the lab; players do not hot reload).

### Where
- Threads started with no handle kept: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L92-L104 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L158-L175
- `on_unload` (no autoupdate shutdown): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2603-L2616
- SporeKit unload: `on_unload`, then hooks, events and commands removed, then `FreeLibrary`; the outside-frame queue is not touched: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/mods.cpp#L70-L86
- The queue holds plain function pointers into the mod: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/console.cpp#L38-L68
- Tasks the core queues every few frames: `menu_tick` (every 0.2 s), `coop_ui::tick` (refresh), `notify::tick`, and `submit_tick` / `own_editor_tick` / `finish_vote`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1360-L1367

### How to reproduce
1. One instance with network access (or offline with a slow DNS), `[update] check=1`.
2. `mod.reload coop` twice within 1-2 s (or run `tools/lab.py reload` twice quickly).
3. Expect, intermittently, a fault with an address inside the old coop.dll range in SporeKit's fault log, or a crash report.
4. Deterministic variant: point `[update] url=` at a local server that accepts the connection and never answers (`nc -l`), start the game, `mod.reload coop`, then stop the server so WinHTTP returns.

### Expected
`on_unload` returns only when no mod thread can run mod code any more, and queued tasks of the unloaded mod are dropped or never point into it.

## Root cause
**What happens**

1. `autoupdate::init` runs on every `sporekit_mod_main` and, with `[update] check=1` (the default; the lanes do not turn it off), calls `CreateThread(check_thread)` and closes the handle.
2. `check_thread` calls `upd::latest_release` and blocks in `WinHttpSendRequest` / `ReceiveResponse` (resolve and connect timeouts 10 s, receive 30 s; offline, DNS can take seconds).
3. `mod.reload coop` within that time: `on_unload` stops the keepalive thread but not this one. Then `FreeLibrary` runs, and SporeKit loads the new shadow copy, often at the same base.
4. WinHTTP returns into `check_thread`'s code, which belongs to the old image, and the thread then writes `g_state` / `g_rel` of the old image. Result: an access violation, or execution of unrelated code of the new image.
5. The same applies to `apply_thread` during "Update" (a download of several MB). It also applies to an outside-frame task queued by the old DLL that has not been pumped when the reload runs: `pump_outside_tasks` postpones the queue while a message is dispatched inside Present, and runs it later.

### Confidence
CONFIRMED by reading the full path (no join, no cancel, no state check at the thread's end). The crash itself is not reproduced here; it needs the timing above.

## Fix
Fixed on fix/w1-core (2bb6835, 5bbb466).
- autoupdate: every thread takes a reference to its own module (`GetModuleHandleExW(FROM_ADDRESS)`)
  and ends with `FreeLibraryAndExitThread`, so the old image stays mapped until the thread is done;
  `autoupdate::shutdown()` (from `on_unload`) keeps an old thread quiet (no log, no feed).
  Old behaviour: `coop.corefix update_pin 0`.
- SporeKit: `run_on_main` / `run_outside_frame` tasks of a mod are tagged with the mod's function
  (`h_run_on_main` / `h_run_outside_frame`); `unload_from` drops the queued tasks whose code lies in
  the image being freed (PE SizeOfImage), before `FreeLibrary` ("dropped N queued task(s) of the old
  image" in the log). No ABI change. Small hunks in sporekit/console.cpp, core.h, main.cpp, mods.cpp.

The review suggested:

- autoupdate: keep the thread handles. Add `autoupdate::shutdown()` called from `on_unload`: set a cancel flag, close the WinHTTP handles (`WinHttpCloseHandle` on the request aborts a blocked call), then `WaitForSingleObject` the threads (bounded, e.g. 35 s, logging if it times out). Alternatively hold a module reference while the thread runs (`GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` at the start and `FreeLibraryAndExitThread` at the end), so the image cannot be freed under it.
- Outside-frame tasks: SporeKit could tag tasks with the queuing mod (`run_outside_frame` has no `SkMod*`, so this needs an ABI addition) and drop them at unload. Or coop could gate every queued task on a generation counter that `on_unload` bumps. That only helps while the code is still mapped, so the SporeKit-side fix is the real one.

## Verification
One-game test `coop_reload_update.py` (lane 2, 2026-10-28): a local server answers the update check
after 8 s; `coop.update check`, then `mod.reload coop` at once.
Observed under Wine: the new shadow copy loads at the old base (the old one is unmapped:
/proc/PID/maps showed only `coop.i3.2.dll` after a reload), so the old thread returned into the
identical code of the new image and finished there with the new image's state: the new image
logged two results ("cannot reach 127.0.0.1 (error 12019)", "(error 6)") instead of its own 404.
No access violation because the code was identical; a different build would fault.
FAIL with `--without-fix` (update_pin 0): two foreign results in the new image. PASS with the fix,
2 runs: one result ("the server answered 404"), console and frames alive, no faults. The queued
task drop was seen at every reload ("dropped 1 queued task(s) of the old image").

The planned test was:

`tools/tests/coop_reload_update.py` (one instance): a local HTTP server that holds the update-check request for 5 s, `mod.reload coop` during that time, release the request, then 10 s of frames. Assert the game is alive (frames advance, no crash report: the lab's crash check) and `coop.update` answers from the new instance. It must fail on main.

### Related
- The umbrella issue #11 (what `on_unload` leaves behind).
- `tools/tests/update_check.py`.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #38.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: pinned update threads, SporeKit drops queued tasks of an unloaded image.
- 2026-10-28: released in 0.7.3.0.
