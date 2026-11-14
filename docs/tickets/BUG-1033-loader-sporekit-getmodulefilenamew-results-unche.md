---
id: BUG-1033
title: loader / SporeKit: GetModuleFileNameW results unchecked (a 0 or a cut path: an overflow of path[MAX_PATH] in the loader, uninitialised names in fault and report lines)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-10, KIT-16
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit
commits: 438e1cdb
tests: kit_crash, diag
signature:
docs: docs/02-modkit.md#modkit-report-safety
related:
---

## Report
Audit 0.7.14, KIT-10 and KIT-16 (one root cause: the result of GetModuleFileNameW is not checked). Not seen at runtime,
found by code reading.
* The loader (steam_api.dll / the proxy DLL, loader/loader.c): when the game's folder path is so long that the DLL's
  path is cut at MAX_PATH, boot_sporekit strips to the last separator of the cut path and lstrcatW appends
  "sporekit.dll": up to 11 wide chars past path[MAX_PATH] on the stack, inside DllMain. A 0 return loads
  "sporekit.dll" by a relative name (the DLL search order) and apply_pending runs with root "" (the pending list is
  looked up from the drive's root). A player can hit it only with a game folder of about 248+ characters (possible
  under Wine and with long paths enabled on Windows).
* SporeKit: exceptions.cpp:97, 269, 312 and diag.cpp:131, 260 build std::wstring from the buffer whatever the call
  returned: a module unloaded between the lookup and the call (diag.cpp:131 enumerates all modules for the report)
  leaves the stack buffer uninitialised: a garbage name or a read past the buffer while a fault or report line is
  made.

## Root cause
Observed in code (main efd2e0ba):
* loader/loader.c:63-69: `n = GetModuleFileNameW(g_self, path, MAX_PATH)`, the strip loop, `lstrcatW(path,
  L"sporekit.dll")`. Untruncated, the file name (steam_api.dll, 13 chars) is longer than sporekit.dll (12), so it fits;
  truncated (n == MAX_PATH) the directory part can be up to 259 chars and the append overflows.
* loader.c:46-53: the same call for the pending root; n == 0 gives root "".
* sporekit/exceptions.cpp:96-98, 268-270, 311-313; diag.cpp:130-131, 259-261: no check of the return value.

Runtime / fix/w49-kit:
Observed in code as described above (no long-path lane: owner decision 5).

## Fix idea
Loader: treat n == 0 or n >= MAX_PATH as "cannot boot" (log through OutputDebugString, start without mods), and check
the length before lstrcatW (or use a 32 768-char buffer and GetModuleFileNameW's long form). SporeKit: one helper
`module_path(HMODULE)` returning "" on 0 / truncation, used at all five places. No switch (a bounds check).

## How to test
Loader: a lane whose game folder is moved under a directory path of 250 characters (tools/spore.sh lane create with a
deep prefix, under Wine): main writes past the buffer (expect a crash at start or a garbled sporekit.dll path in the
debug output): FAIL; with the fix the game starts without mods and logs why. SporeKit part: no practical FAIL
(`no-fail-run: needs a module unloaded mid-call`).

## Fix
loader/loader.c: boot_sporekit and apply_pending treat a 0 or MAX_PATH result of GetModuleFileNameW as "cannot boot" (a
debug line, the game starts without mods / no pending swap), and boot_sporekit checks the room for "sporekit.dll"
before lstrcatW. SporeKit: `module_path(HMODULE)` (core.h) returns "" on 0 or a cut path; the fault logger
(in_our_code, the fault's place, the module calls), the reports (module list, where()), d3d9's module_text and the
stack command use it. No switch (bounds checks). Not changed: main.cpp's and symbols.cpp's reads of the exe's own path
(the loader no longer boots SporeKit when that path is cut).

## Verification
no-fail-run: a game path of about 250 characters (owner decision 5: not worth a lane) and a module unloaded between the
lookup and the call (a race); the bounds checks are read in the diff. Regression: kit_crash and diag PASS with
module_path in the reports: out/lab/20261113-011055 (kit_crash, kit_watchdog, diag).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit (no-fail-run, owner decisions 4/5).
- 2026-11-14: released in 0.7.14.
