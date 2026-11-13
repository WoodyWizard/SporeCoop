---
id: BUG-1074
title: spacecoop: the planet seeding's per-thread guards (GenState generating / in_profile) stay set after a fault the game catches inside the generation
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: BUG-1047 audit (fix/w49-guard)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1047, BUG-0123, BUG-0185
---

## Report
Found by code reading while fixing BUG-1047 (not seen at runtime). spacecoop.cpp's planet seeding keeps its guards in a
thread_local GenState (BUG-0123): `g.generating = true; o_StoreFill / o_RequirePlanets; g.generating = false;` and
`g.in_profile++; o_SpeciesProfile; g.in_profile--;` (h_SpeciesProfile). A fault inside these calls that the game
catches leaves them set on that thread: with `generating` stuck every later draw of that thread is seeded (the
galaxy's other random draws on that thread follow the planet seed); with `in_profile` stuck the profile draws of a later
generation stay on the global generator, the planets of a star may differ from the partner's again (BUG-0185).

## Root cause
Observed in code: the bare pairs at spacecoop.cpp h_SpeciesProfile, the store seeding (o_StoreFill) and
h_RequirePlanets. Inferred: that these calls can fault and be caught. Not covered by BUG-1047's registry on purpose:
the state is thread_local (a static registration would hold the loader thread's copy) and the generation runs on
other threads than the frame's, so a reset once a frame on the main thread cannot clear it.

## Fix idea
A per-thread reset at the start of each outermost generation call (generating / in_profile are 0 there unless a fault
left them), or a callguard::Hold-like guard that records the thread's entry depth.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
