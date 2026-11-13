---
id: BUG-1045
title: Space: "Fly to" a partner at the galactic core from the galaxy view teleports without the energy refund and without the disabled check (TravelToStar spends energy, damages an empty ship)
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-11
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-finale-fly-energy
related: BUG-0063, BUG-0820
---

## Report
Audit SPC-11. Not seen at runtime, found by code reading.
At the galactic core finale, "Fly to" a partner while in the galaxy view moves the ship with SpaceTeleportTo directly: the flight costs energy (TravelToStar), and with no energy left it damages the ship; the co-op jump elsewhere is free. A ship that got disabled after the order (up to 30 s before the step) still flies.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/space_finale.cpp:455-482 `fly_step`, context 2 (galaxy view): `cdecl_call<void>(a_Teleport, rec)` at 472, no energy save / restore of ufo +0x7F4 / +0x7F8, no `spacecoop::disabled()` check, no own-ship cinematic check beyond `busy()`.
* `fly_to_star` (space_finale.cpp:507-523) checks `spacecoop::disabled()` only when the flight is queued; fly_tick runs the step later (until +30 s).
* The same call elsewhere has all of it: spacecoop.cpp:488-515 `teleport_in_galaxy` (BUG-0063: disabled, cinematic, energy refund) and travel_to (620-636).
Inferred: the amount of energy a core-sized jump costs. Confidence: high.

## Fix idea
Make fly_step's galaxy branch call `spacecoop::teleport_in_galaxy(star)` (it has the guards and the refund) and report its error as the other branch does. Switch: `coop.fix space.finale_fly_free off` (the direct call).

## How to test
`coop_space_finale.py` (or a new `coop_space_finale_fly.py`): both near the core, the guest in the galaxy view with energy set low (`coop.space energy 5`), `Fly to` the host (the finale command the button calls); assert the guest's ship energy and hp after the flight (`coop.space energy`): main spends energy / loses hp (FAIL), fixed keeps them. Second case: `coop.space down` (disabled) before the step: no flight.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
