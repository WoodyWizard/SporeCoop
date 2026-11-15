---
id: BUG-1060
title: tests: coop.memcensus initialises its tracker without a lock and its free hook keeps working after the tracker is off
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit2
commits: 702f51dd
tests: make test
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

Observed vs inferred (2026-11-13, fix/w49-kit2): observed in code; two first `track on` at once were never seen (inferred).

## Fix idea
A std::once_flag (or a static mutex) around the init; h_free tests g_on; remove the hooks on `track off` or at
unload. Not a player path: no switch.

## How to test
no twoinst test needed: a unit-level run of `coop.memcensus track on` from two console connections at once and
`track off`, then a census: no crash, the free hook idle (a counter of forgets after off stays 0).

## Fix
mods/coop/mem_census.cpp: track() runs under a static mutex (one critical section, TLS index, table and hook
pair, however many consoles); h_free tests g_on, so after `track off` the hook only reads the flag; `track on` after
an off starts with an empty table (the frees in between were not seen). The table and the TLS index stay for the run
(a hook may still be inside them until SporeKit removes the hooks at the unload). No switch (diagnostic only).

## Verification
no-fail-run: a first-use race of a diagnostic command from two consoles; not reproducible on demand, fixed by
inspection. Build passed; the census tests (extended, 30 min) were not run for this change (lab time budget).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (no-fail-run, decision 4).
- 2026-11-14: released in 0.7.14.
