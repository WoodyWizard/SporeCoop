---
id: BUG-1059
title: tests: coop.drama reads stale event pointers and the drama hook logs the wrong missing symbol
status: fixed
kind: tooling
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-life
commits: f2f013ec, a49f379c, ade567a3, a7f75467, 398a5ab1
tests: lifecycle_reset
signature:
docs: docs/01-architecture.md#arch-lifecycle-registry
related: BUG-0736, BUG-0800
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-23 (drama.cpp). Test and debug only; found by code reading.
`coop.drama` (tests) prints `ev[0xC]` of the last event pointer of each kind, kept from h_Start in any earlier world
or session (the event objects belong to the drama manager of that world): a read of freed memory after a load. The
install logs "DramaManager_StartEvent missing" when SummonEvent_Update is the symbol that is missing.

## Root cause
Observed in code (main efd2e0ba), confidence high:
mods/coop/drama.cpp:35 `static void* g_last_event[9];` set in h_Start (drama.cpp:86), never cleared; read in debug
(drama.cpp:233-238) without skg::readable. drama.cpp:148-151: the else branch of the SummonEvent_Update lookup logs
the name of the other symbol.

## Fix idea
Clear g_last_event at a world change / session end (lifecycle registry) and read through skg::readable; fix the log
text. No switch needed (debug output).

## How to test
no player test; a unit-level check: `coop.drama` after a world load prints no event pointer of the old world
(a run of any creature test that loads twice, asserting the event column is empty after the load).

## Fix
drama.cpp: the last event per kind is entry `drama.last_events` of the lifecycle registry (world change), coop.drama
reads the event's +0xC through skg::readable, and the install logs the missing symbol it means
(DramaManager_StartEvent or SummonEvent_Update). No switch (debug output, as the ticket says).

## Verification
no-fail-run: test and debug output only (coop.drama); the reset is the lifecycle registry's (lifecycle_reset checks
every entry after each world change, out/lab/20261113-011223), the log text is not testable without a missing
symbol.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-life (package L1, the lifecycle registry of BUG-1055).
- 2026-11-14: released in 0.7.14.
