---
id: BUG-0195
title: Space map: a guest's colonies last only until it rejoins
status: open
kind: feature
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
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
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-30 (fix/w13-space): left open, not a contained change for the 0.7.3.2 patch: adopting the guest's colony in the host's world collides with the allies mirror (undone before every save), needs the planet records (BUG-0185) and loses the colony's state. Design for Rivals 0.8 written: docs/11-rivals.md#rivals-guest-colonies.
