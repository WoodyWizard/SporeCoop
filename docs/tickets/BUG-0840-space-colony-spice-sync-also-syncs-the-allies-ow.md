---
id: BUG-0840
title: Space: colony spice sync also syncs the allies' own-star mirror colonies and can wipe a guest's stored spice (suspected)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: 41614516
tests: space_spice_mirror
signature:
docs: docs/re/space.md#space-s1-0713
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
The host's colony list for the sync (sent values and applied collections) leaves out the planets that are the
mirror of a partner's own colony in a shared star system (spaceext::allies_own_mirror). Root cause confirmed at
runtime (it was inferred): the host's mirror of the guest's colony 01460035 has 2 cities with 0.0 spice, the same city
count as the guest's colony, so the host's K_SPICE wrote 0.0 into the guest's real colony. Switch
`coop.fix space.colony_spice_mirror off` (0.7.12). `coop.space colonyspice`: `mirrors_skipped=` (counts per tick).

## Verification
`coop_space_colony_spice.py --case mirror` (suite `space_spice_mirror`): the guest colonizes a free planet of the home
system (`allies colonize`), its colony stores 40 per city (`colonyspice set`), 30 s.
* FAIL with `SPORE_FIX_OFF=space.colony_spice_mirror`: lab 20261112-034846 ([40.0, 40.0] -> [0.0, 0.0]).
* PASS: lab 20261112-034405 (40.0 / 40.0 after 30 s; the host's mirror 0.0 / 0.0).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the host leaves a partner's own colony out of the sync; switch space.colony_spice_mirror).
