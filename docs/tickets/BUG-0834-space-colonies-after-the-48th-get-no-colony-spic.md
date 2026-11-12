---
id: BUG-0834
title: Space: colonies after the 48th get no colony spice sync (silent cap; none at all above 256)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: 41614516 52f5971d
tests: space_spice_cap
signature:
docs: docs/re/space.md#space-s1-0713
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
The colony list for the sync has no 48 cap any more (a sanity limit of 1024 list entries, before 256 returned none);
the host's K_SPICE goes in parts of at most 48 colonies (the u8 count, a guest applies each part to the colonies it
names). Switch `coop.fix space.colony_spice_all off`: the cap (0.7.12). The cut is counted (`cut=` in
`coop.space colonyspice`).

## Verification
`coop_space_colony_spice.py --case cap` (suite `space_spice_cap`, fixture space_empire: three colonies on both games).
Fifty colonies cannot be made in a test save (colonies only through `coop.space empire colony` per planet record, a
generated planet per star), so the test command `coop.space colonyspice cap 1` sets the old cap (used only with the
switch off) to 1: the second and third colonies stand for the 49th and later. The host's third colony 00460014 stores
30 spice: the guest's game must get it.
* FAIL with `SPORE_FIX_OFF=space.colony_spice_all`: lab 20261112-035831 (the guest's 00460014 stays at 5.0). A first
  FAIL run (20261112-034947) failed for the wrong reason: the test listing used the cap too and showed one colony;
  the listing is uncapped since 52f5971d.
* PASS: lab 20261112-035834 (30.0 on the guest).
* PASS again: lab 20261112-035922; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (no cap, K_SPICE in parts; switch space.colony_spice_all).
