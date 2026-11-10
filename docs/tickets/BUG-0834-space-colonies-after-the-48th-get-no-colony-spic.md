---
id: BUG-0834
title: Space: colonies after the 48th get no colony spice sync (silent cap; none at all above 256)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0537
---

## Report
In a large late-game empire the shared colonies past the 48th can be collected by every player again (the BUG-0537 double collection). Found by reading the code; low impact.

## Root cause
Observed in the code, confidence high. space_planets.cpp:49 and :76 kMaxColonies = 48 cuts silently; :74-75 return no colonies at all above 256. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Drop the cap (reliable messages are fragmented) or at least log when it cuts.

## How to test
Give the host's empire 50+ colonies (debug colonize loop), store spice in colony 50: FAIL = `coop.space colonyspice` lists 48 and the guest can collect colony 50 again; PASS = all listed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
