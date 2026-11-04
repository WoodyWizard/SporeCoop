---
id: BUG-0195
title: Space map: a guest's colonies last only until it rejoins
status: fixed
kind: feature
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.6
branch: fix/w42-space-life
commits: a8504fa0
tests: space_colony_rejoin, space_map
signature:
docs: docs/11-rivals.md#rivals-guest-colonies
related:
---

## Report
A list of limits and open ends of a feature, kept as one ticket.

Copied from docs/known-issues.md, where the entry stays up to date:

* Every player flies in a copy of the host's save: a colony the guest founds exists only in the
  guest's running game. When the guest leaves and joins again it loads the host's world and the
  colony is gone (`coop_space_map.py` checks exactly this). The host's own colonies survive (they
  are in the save the guests get). Shared colony ownership is not done (docs/re/space.md 5.3).
* The names of stars nobody named differ between the copies (docs/re/space.md 13.1): the roster
  sends the owner's names, the map shows mine for stars without a roster name.
* The test colony is founded with `coop.space map colonize` (the tool's effect without the aiming
  check): the real aiming refused every planet of the `space` fixture but the home planet ("Can't use
  in this system", "Colonies must be placed near spice"). A fixture with a planet with spice
  geysers would let the test aim like a player.
* The map is overlay text (a ring of dots, a name tag), not the game's own star marks; tags are not
  hidden behind the game's own galaxy panels (the star info card), only under the pause menu, the
  co-op window, comm screens and cinematics.

## Root cause
* Observed (lab 20261104-095758, `SPORE_FIX_OFF=space.keep_colonies`): the guest's record-level colony on
  P01460035 (tech 5, a cCivData of its empire) is tech 1 with no civilization after it left, joined again and
  loaded the host's world (its Space world epoch 1 -> 2). Every player flies in a copy of the host's save; the host
  shows the guest's colony only as the allies mirror, undone before every save, so the share save never has it.
* Observed (lab 20261104-100007, the first version of the fix): the guest's first roster after a join comes from
  its own world, before the host's world is loaded; a list applied then was lost with the load.
* Test pitfall found on the way: right after `coop.leave` + join the guest still answers `coop.avatar` in its old
  world, so `wait_guest_in` returned before the load (the first FAIL run, 20261104-095633, read the old world and
  passed): the test waits for the guest's Space world epoch to move on.

## Fix
`space.keep_colonies` (space_map.cpp, MSG_SPACE kinds 12 K_KEPT / 13 K_KEPT_DONE of the map feature, no new
MSG id): the host keeps each guest's own colonies (its roster's colonies the host's roster does not have) by the
player's name for the Space world it plays (reset when the host's Space world epoch changes). On that player's
first roster after a join the host sends the list and stops taking its rosters for it until the guest answers.
The guest applies the list once a world was loaded since the join (the host's share): each planet becomes a
record-level colony of its own empire (`spaceext::colonize_own`, space_allies.cpp: star claim, tech 5,
FillPlanetDataForTechLevel, AddPlanet; "wait" while the ship is on that planet; a star of another empire or a
colony of another empire now is refused and reported back, the host drops it), says "Your colonies are back in
your empire." and sends its roster at once. `coop.space map kept` shows both sides.
Not done (docs/11-rivals.md#rivals-guest-colonies): memory only (a host that restarts its game keeps nothing; the
side file of the design is not written); the colony comes back with one city and no stored spice (the guest's
cities and spice are not kept); keyed by the player's name (a guest that rejoins under another name is another
player).

## Verification
* FAIL (`SPORE_FIX_OFF=space.keep_colonies`): out/lab/20261104-100358 (and 20261104-095758 on the build before): after the rejoin the planet is tech 1, no
  civilization, the host's roster of the guest has no own colony.
* PASS: out/lab/20261104-100253: the rejoined guest's game has P01460035 as a colony of its empire again (tech 5,
  civ pol of its empire), the host's roster of it lists the colony as its own.
* Regression: space_map out/lab/20261104-100601 (its last check now expects the colony back).

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-30 (fix/w13-space): left open, not a contained change for the 0.7.3.2 patch: adopting the guest's colony in the host's world collides with the allies mirror (undone before every save), needs the planet records (BUG-0185) and loses the colony's state. Design for Rivals 0.8 written: docs/11-rivals.md#rivals-guest-colonies.
- 2026-11-04: fixed on fix/w42-space-life (space.keep_colonies, memory-only version of docs/11-rivals.md#rivals-guest-colonies), test space_colony_rejoin.
- 2026-11-04: released in 0.7.6.
