---
id: BUG-9268
title: Galaxy Tribe planet: the host's own reload (empty village rescue) is read as leaving: the planet is handed over
status: open
kind: bug
stage: tribe
reported: 2026-11-19
source: QA run 2026-11-19 (BUG-9266 tests)
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in:
branch: feat/lineages-alpha
commits:
tests:
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
Inferred: world::load_slot's reload of the planet being played goes through the galaxy menu (the only safe load
path), and galaxy_client treats any arrival in the galaxy menu while hosting as leaving the planet. Which Tribe path
asks for the reload (the village rescue or the share after a new founder seat) is not confirmed.

## Fix
Not fixed. Likely: the galaxy client ignores the menu while world::load_slot's reload of the same planet is under way
(it re-hosts the planet it was hosting).

## Verification

## History
- 2026-11-19: opened (found while testing BUG-9266).
