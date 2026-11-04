---
id: BUG-0197
title: Space allies: what the mirror of a partner's colonies does not do (space_allies.cpp)
status: open
kind: feature
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0543
---

## Report
A list of limits and open ends of a feature, kept as one ticket.

Copied from docs/known-issues.md, where the entry stays up to date:

* Only stars nobody owns in my galaxy are mirrored. A partner's colony in a system that is mine or
  an NPC empire's in my copy is not shown (`coop.space allies list`: "skipped ... a star system of
  another empire here"); in the usual session (the guest plays a copy of the host's save) all
  colonies both had at the join are shared and need no mirror.
* The mirror empire has my own species (citizens, buildings, portrait): the partner's species is
  not in my files when the saves differ. Its colour is the palette colour nearest to the partner's.
* A mirror colony gets the game's own record-level colony: one city on landing (10 cCityData), not
  the partner's layout, buildings or spice.
* The relationship entry of the mirror (flags cleared) stays in a save, like the game's own entries
  of destroyed empires (there is no erase). The political id counter is not given back.
* Raids: only a raid while I am on that planet (the owner elsewhere) gets a raid of my own; the
  game's alert there says "your cities". Plunder raids (0xC4956AA7) are mirrored like raids
  (untested). A raid on a guest's colony helped by the host is untested (squadron makes the host's
  NPCs authoritative on both sides there).
* A partner who leaves stays mirrored ("away") until my session ends; with two players the host
  leaving ends the session, so the frozen ally is only seen on the host (a guest left) or with 3+
  players (untested).
* Rivals mode (feature 10): a war between the players makes the mirror an enemy (relationship war
  bit, enemies vectors); not tested here (feat/space-rivalry not merged).
  ~ superseded (2026-10-27): space_rivalry.cpp is on main now; whether this path is tested was not
  re-checked, BUG-0153.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-11-04 (fix/w42-space-play): the case players meet most (a partner's new colony in a star system both
  own since the join, "your own star system") fixed as BUG-0543 (allies.own_star, test space_allies_own): a
  record-level colony of my empire, undone before saves. Still open from this list: a colony in an NPC empire's
  system in my copy; the mirror's species is mine; a mirror (or own-system) colony has the game's one city, not the
  partner's cities, buildings or stored spice; raids on an own-system copy are not mirrored (untested); the frozen
  ally after a leave; rivals mode with the mirror as an enemy (not re-checked).
