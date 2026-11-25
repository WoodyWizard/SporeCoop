---
id: BUG-9268
title: Galaxy Tribe planet: the host's own reload (empty village rescue) is read as leaving: the planet is handed over
status: fixed
kind: bug
stage: tribe
reported: 2026-11-19
source: QA run 2026-11-19 (BUG-9266 tests)
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: d9c4da12
tests: manual on TITAN QA 8 + 9 (the host reloads Samuth with coop.load: the same world::load_slot path)
signature: "world: leaving the stage to the galaxy menu before loading" then "galaxy: You left <planet> for the menu"
docs: docs/known-issues.md
related: BUG-9147
---

## Report
QA run 2026-11-19 (TITAN QA 8 hosting the galaxy Tribe planet Jiff Peanutbutter, QA 9 joined): 15 s after the join the
host's village had no living member, the game brought a villager back (BUG-9147), saved, shared the world with the
guest and reloaded it through the galaxy menu ("world: leaving the stage to the galaxy menu before loading Jiff
Peanutbutter.spo"). The galaxy client read the menu as the host leaving: "You left Jiff Peanutbutter for the menu:
saving it to the galaxy and handing it over". The guest became the host, then left too; the planet was forgotten.

## Root cause
✔ world::load_slot's reload of the planet being played goes through the galaxy menu (the only safe load path), and
galaxy_client treated any arrival in the galaxy menu while hosting as leaving the planet (owner 2026-11-17: leaving to
the menu hands the planet over). The Tribe reload was the seat rebuild (world.cpp: "party: 7 seat(s)", every player
shared the world, then load_slot): every new player on a stored Tribe or Civ planet whose founder seats must grow.

## Fix
world::menu_load_planet() names the save a menu load is bringing in (queued, or posted under 15 s ago); while it is
the hosted planet's own file, the galaxy client keeps hosting ("the game reloads X through the menu: still its host").
Switch `coop.fix galaxy.reload_stays`.

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN: QA 8 hosted Samuth, QA 9 joined, the host ran
`coop.load Game11 Samuth` (world::load_slot, as the rebuild):
* fix off (FAIL): "You left Samuth for the menu: saving it to the galaxy and handing it over"; the server handed
  Samuth to QA 9, QA 8 was on no planet.
* fix on (PASS): "the game reloads Samuth.spo through the menu: still its host"; after the reload QA 8 hosted Samuth
  (role host), QA 9 still its guest, menu hand-overs 0, own reloads kept 1.

## History
- 2026-11-19: opened (found while testing BUG-9266); fixed.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
