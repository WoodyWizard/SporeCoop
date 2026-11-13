---
id: BUG-1031
title: SporeKit: ftrace changes its filter string while the CreateFileW hook reads it on other threads
status: open
kind: bug
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-9
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
Audit 0.7.14, KIT-9. Not seen at runtime, found by code reading. `ftrace <substring>` (a debug console command)
assigns the global std::wstring g_ftrace on the main thread while the CreateFileW detour reads it (empty(),
c_str() into wcsstr) on every thread that opens a file (the game's loaders, audio streaming, our own threads). A
reassignment frees the old buffer under a reader: a read of freed memory or a fault in the detour. Debug only:
players do not use the console.

## Root cause
Observed in code (main efd2e0ba): sporekit/probe.cpp:141 `static std::wstring g_ftrace;`, 143-147 the detour reads it
without a lock, 168-174 ftrace_set assigns it (commands.cpp:635, `ftrace` is SK_CMD_MAIN_THREAD). The hook, once made,
stays for the run.

## Fix idea
A std::shared_ptr<const std::wstring> swapped atomically (std::atomic_load / store) or a fixed wchar_t buffer behind a
mutex taken by both; skip the work when the filter is empty without touching the string (an atomic flag). No switch
needed.

## How to test
Debug-only path; a lab stress test `coop_ftrace_race.py` (one lane): `ftrace a` / `ftrace b` 1000 times in a loop
while the game loads a stage (many CreateFileW calls); assert no fault in `exc` and no crash. Expect a weak FAIL on
main; otherwise `no-fail-run: race, not reproducible`.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
