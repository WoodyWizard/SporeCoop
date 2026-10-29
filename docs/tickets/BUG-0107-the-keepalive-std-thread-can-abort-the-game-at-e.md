---
id: BUG-0107
title: the keepalive std::thread can abort the game at exit when Shutdown did not run
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#86
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: 066df15
tests: tools/tests/coop_exit_thread.py (suite exit_thread)
signature:
docs:
related: #86
switch: core.keepalive_ptr
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/86 (full text, discussion).

`coop.dll` keeps its network keepalive thread in a global `std::thread g_keepalive`. Only `on_unload` joins it. `on_unload` runs on hot reload and from the `App::cSporeApp::Shutdown` hook. If the process ends through `ExitProcess` without that hook having run, the thread object is still joinable when `coop.dll`'s static destructors run in `DLL_PROCESS_DETACH`. `std::thread::~thread()` on a joinable thread calls `std::terminate()`, which calls `abort()`. So the end of the game becomes an abort: an "abort() has been called" box or a fast-fail / WER crash, on top of whatever ended the game.

### Where
- The global and its start: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L914-L940
- The only join, `keepalive_stop`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L942-L947
- Its only caller, `on_unload`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2603-L2616
- `on_unload` at exit comes only from the Shutdown hook (`mods_shutdown`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/gamehooks.cpp#L59-L65
- The thread is started from `sporekit_mod_main`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2692
- The exit paths SporeKit already knows about (`ExitProcess` / `TerminateProcess` hooks): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L661-L677

### How to reproduce
1. Start a game with co-op loaded: the keepalive thread starts at mod load.
2. End the process with `ExitProcess` from the main thread without `cSporeApp::Shutdown`. For example, `call ExitProcess stdcall 0`, using the kernel32 address taken from `sym`/`mem.ptr`, or a test-only console command that calls `ExitProcess(0)`.
3. Expected with the bug: the process ends with an abort or fast-fail (code 3 or `0xC0000409`) instead of exit code 0.
4. The window is "always" while co-op is loaded. No timing is needed.

### Expected
The game ends with its own exit code. No abort, no extra crash dialog.

## Root cause
**What happens**

1. The game calls `ExitProcess` without going through `cSporeApp::Shutdown`.
2. Windows terminates every other thread, the keepalive thread included. The `std::thread` object still holds its id, so it is still joinable.
3. `DLL_PROCESS_DETACH` for `coop.dll` runs the atexit table. `coop.dll` is linked `-static` and mingw-w64's `crtdll.c` runs `_execute_onexit_table` on detach.
4. `~thread()` finds it joinable, calls `std::terminate()` and then `abort()`. Under UCRT that means `__fastfail(FAST_FAIL_FATAL_APP_EXIT)` or a runtime error box, and exit code 3.

`docs/02-modkit.md` (Crash reports) describes the game closing itself after its own handler caught a fault. That is the most likely exit without Shutdown. A plain `exit()` from game code would do the same.

### Confidence
- Destructor behaviour: CONFIRMED by reading.
- The exit path: PLAUSIBLE. It is not verified that the game's fault handler exits without Shutdown.

A second reviewer checked the destructor and CRT detach behaviour. Severity: low.

## Fix
Fixed on fix/w1-core (066df15). The keepalive thread is a `std::thread*` created with `new`;
`keepalive_stop` joins and deletes it (hot reload, Shutdown). At an exit without Shutdown nothing is
destroyed, so `~thread()` never sees a joinable thread. Old behaviour: `coop.exittest old` (starts
the old global `std::thread` and exits).

The review suggested:

Do not keep a joinable `std::thread` in a global. Any of these works:
- `detach()` it right after creation and stop it with the atomic flag.
- Start it with `CreateThread` and keep the `HANDLE`.
- Hold it through a pointer that is never destroyed.

`keepalive_stop` can still wait for it on hot reload, through an event or the handle.

## Verification
One-game test `coop_exit_thread.py` (lane 2, 2026-10-28): `coop.exittest` calls `ExitProcess(0)` on the
main thread without `cSporeApp::Shutdown`; a SIGABRT handler writes "abort" into
mods/logs/coop_exittest.txt.
FAIL with `--without-fix` (`coop.exittest old`): the marker reads "exit 0 | abort", so the CRT's
DLL detach destroyed the joinable global std::thread and std::terminate -> abort ran (CONFIRMED in
the game, Wine). PASS with the fix, 2 runs: marker "exit 0", SporeKit logged "the game exits
(ExitProcess 0)".

The planned test was:

A tools/tests check that starts a game with coop.dll, calls `ExitProcess(0)` through the devkit console, and asserts on the process exit code (0, not 3 or `0xC0000409`) and that no crash report or last-problem mark was written. On main it should fail.

### Related
#38 (threads and queued tasks of coop.dll around an unload), #11 (cleanup differs between leave, re-host and reload).

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `exit_nofix.log` (fix/w1-core), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261027-192300-fix-w1-core on fix/w1-core (exit_thread ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #86.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: keepalive thread through a pointer; abort at exit confirmed by coop_exit_thread.py.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: core.keepalive_ptr (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
