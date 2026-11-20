---
id: BUG-9276
title: Civ own nations: a partner who gets a species of their own on a planet with no free city site gets no nation (it shares the host's)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-20
source: found in the port of BUG-9274 (lane C, 2026-11-20)
role: 
versions: 0.7.15.5 (feat/lineages-civ)
fixed_in: unreleased
branch: feat/lineages-civ
commits:
tests: tools/tests/coop_civ_nosite.py (suite civ_nosite), tests/civ_seats_tests.cpp (adoption)
signature:
docs: docs/re/civ.md#civ-own-nations docs/re/civ.md#civ-nation-adopt
related: BUG-9274
---

## Report
With every player's own nation (BUG-9274), the host founds a partner's capital at the game's next free city site. On a
planet whose ten sites are all founded (the dev Kealeneon Civ save on lane C: 10 cities, cCivExpansion +0xB0 = 0) a
partner who joins with a creature of their own gets no nation: the host logs "Bob's capital waits: no free site" and the
partner plays the host's nation (the 0.7.15 split).

## Root cause
Observed: SpawnNextCity takes only a free site; none is left. Lineages had the same limit (its late capital: "No site
left: a notice").

## Fix
Switch `civ.nation_adopt` (default on): when the next seat's capital finds no free site, the host takes over an NPC
nation for it (civ_seats::adopt_choice: not the host's, not in the roster, every city settled; the fewest cities, then
the nearest to the host's capital) and binds it as a founded capital (bind(..., adopted)): the seat's species key on
the nation and its cities (+0x268, city +0x658), the roster entry, the world shared again. Its money starts as a
founded capital's: the host nation's (never less than the NPC's own GetMoney total, moved into +0x98). None to take
(only the players' nations left): the partner waits as before. Debug: `coop.civ nations sites` (the site records),
`adopt_city` / `adopt_money` test knobs. Docs: docs/re/civ.md#civ-nation-adopt (also the site record layout:
+0x04..+0x0C the position).

## Verification
no-fail-run: lane C by hand (no Linux lab here), the dev Kealeneon save (10 of 10 sites), two instances: with
`coop.fix civ.nation_adopt off` the guest's seat got no nation ("Bob's capital waits: no free site" in the logs of the
16:49 .. 18:27 runs; the 20:27 run: no nation either); switched on, within 20 s the guest's nation was NPC nation
000021D0 of its species, its money the host nation's 23170, allied 8.0 both ways, the city "[Bob]" and its 19 citizens
of the guest's species on both games, the guest's vehicle purchase paid by its nation (HUD 23227 -> 22231) and its
move order driving it on the host's game, the host's capture refused, a leave and rejoin the same nation (none taken
twice). Screenshots out of git (scratch civ/c3/s3_*_city.png). The lab test coop_civ_nosite.py was not run (no Linux
lab).

## History
- 2026-11-20: opened (lane C, Kealeneon).
- 2026-11-20: fixed on feat/lineages-civ (civ.nation_adopt); a taken nation starts with the host nation's money.
