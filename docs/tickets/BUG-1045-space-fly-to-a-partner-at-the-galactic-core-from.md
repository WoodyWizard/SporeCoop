---
id: BUG-1045
title: Space: "Fly to" a partner at the galactic core from the galaxy view teleports without the energy refund and without the disabled check (TravelToStar spends energy, damages an empty ship)
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-11
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-space
commits: f95e96fe
tests: space_finale_fly
signature:
docs: docs/re/space.md#space-war-npc-hits, docs/known-issues.md#issue-finale-fly-energy
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
space_finale.cpp, switch `coop.fix space.finale_fly_free` (off: 0.7.13): fly_step's galaxy-view branch calls
spacecoop::teleport_in_galaxy (BUG-0063: refused while the ship is disabled or a cinematic plays, the energy kept);
a refusal is told ("Can't fly there: ...") and counted (`fly_refused=` in `coop.space finale`). Test option:
`coop.space finale goto <star> [enter] delay <s>` runs the step s seconds later.

## Verification
`coop_space_finale_fly.py` (suite `space_finale_fly`, fixture space, ~1 min): the guest gets the drive, zooms out to the
galaxy view with the wheel, `coop.space finale goto near` (what "Fly to" queues) with full energy, back home with
energy 0, then the order with `delay 4` and the ship disabled before the step.
* FAIL (SPORE_FIX_OFF=space.finale_fly_free): out/lab/20261113-013833-2 (energy 0: hp 1500 -> 1300; the disabled ship
  flew to the star near the core)
* PASS: out/lab/20261113-013622, out/lab/20261113-015737-2 (hp 1500 -> 1500; the disabled ship stayed)
Observed: with full energy the direct call left the energy at 100 of 100 (no cost seen for this jump); the visible
harm was the 200 hp taken with no energy, and the disabled ship's flight.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space: the galaxy step goes through teleport_in_galaxy; FAIL/PASS in the lab.
