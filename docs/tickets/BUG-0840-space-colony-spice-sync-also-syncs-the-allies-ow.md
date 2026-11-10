---
id: BUG-0840
title: Space: colony spice sync also syncs the allies' own-star mirror colonies and can wipe a guest's stored spice (suspected)
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
related: BUG-0197
---

## Report
Suspected from the code: a guest that colonized a planet in a shared star system may see its stored spice reset to about 0.

## Root cause
Inferred, confidence medium. space_planets.cpp colonies() (:66-96) reads the empire's planet list, which add_planet_own (space_allies.cpp:572-577) extends with the mirror; it never checks allies_own_mirror. The host then broadcasts the mirror's near-zero spice for the guest's real colony. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Skip allies_own_mirror colonies on both sides of the colony spice sync.

## How to test
Guest `coop.space allies colonize <free planet in the home system>`, store spice, watch `coop.space colonyspice` on the guest for 30 s: FAIL = its spice drops to the mirror's; PASS = unchanged.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
