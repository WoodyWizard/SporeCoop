---
id: BUG-1059
title: tests: coop.drama reads stale event pointers and the drama hook logs the wrong missing symbol
status: open
kind: tooling
stage: Creature
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
