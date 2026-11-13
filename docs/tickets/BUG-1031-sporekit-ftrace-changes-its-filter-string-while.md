---
id: BUG-1031
title: SporeKit: ftrace changes its filter string while the CreateFileW hook reads it on other threads
status: fixed
kind: bug
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-9
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit2
commits: 702f51dd
tests: make test
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

Observed vs inferred (2026-11-13, fix/w49-kit2): the unguarded string observed in code; a fault in the detour was never seen (inferred).

## Fix idea
A std::shared_ptr<const std::wstring> swapped atomically (std::atomic_load / store) or a fixed wchar_t buffer behind a
mutex taken by both; skip the work when the filter is empty without touching the string (an atomic flag). No switch
needed.

## How to test
Debug-only path; a lab stress test `coop_ftrace_race.py` (one lane): `ftrace a` / `ftrace b` 1000 times in a loop
while the game loads a stage (many CreateFileW calls); assert no fault in `exc` and no crash. Expect a weak FAIL on
main; otherwise `no-fail-run: race, not reproducible`.

## Fix
sporekit/probe.cpp: the filter lives behind an SRW lock: the CreateFileW detour reads it shared (only when the
atomic g_ftrace_on says a filter is set, so with no filter it takes no lock), ftrace_set swaps the new string in
exclusively and frees the old one after the lock. No switch (debug command; the old side is a use after free).

## Verification
no-fail-run: a use-after-free race in a debug-only command (`ftrace`) against the game's file threads, not
reproducible on demand; fixed by inspection. Build and regression out/lab/20261113-083757 (notify, hotreload, reload_update, kit_cmd_fault) passed.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (no-fail-run, decision 4).
