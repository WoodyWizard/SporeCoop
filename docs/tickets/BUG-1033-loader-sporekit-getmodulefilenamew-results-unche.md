---
id: BUG-1033
title: loader / SporeKit: GetModuleFileNameW results unchecked (a 0 or a cut path: an overflow of path[MAX_PATH] in the loader, uninitialised names in fault and report lines)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-10, KIT-16
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
