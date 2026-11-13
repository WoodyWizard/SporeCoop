---
id: BUG-1060
title: tests: coop.memcensus initialises its tracker without a lock and its free hook keeps working after the tracker is off
status: open
kind: tooling
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
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
Audit 2026-11-12 for 0.7.14, finding WLD-23 (mem_census.cpp). Test and diagnostic only (`coop.memcensus`); found by
code reading. The tracker's first `track on` initialises a critical section, a TLS index and its table without a lock
(the command is SK_CMD_ANY_THREAD, so two consoles could race); after `track off` the NtFreeVirtualMemory hook still
takes the critical section on every MEM_RELEASE of the process (h_free tests g_tab, not g_on); the table and the TLS
index are never released.

## Root cause
Observed in code (main efd2e0ba), confidence high:
mods/coop/mem_census.cpp:220 track(): lazy InitializeCriticalSection (:227), TlsAlloc (:231), VirtualAlloc (:233) and
hook_create (:268-270) without synchronisation; :205 h_free `if (r >= 0 && g_tab && ...) forget(...)` keeps working
after `track off` (:223 only sets g_on false). The unload side (a command running while the DLL is unloaded) is the
SporeKit finding KIT-1.

## Fix idea
A std::once_flag (or a static mutex) around the init; h_free tests g_on; remove the hooks on `track off` or at
unload. Not a player path: no switch.

## How to test
no twoinst test needed: a unit-level run of `coop.memcensus track on` from two console connections at once and
`track off`, then a census: no crash, the free hook idle (a counter of forgets after off stays 0).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
