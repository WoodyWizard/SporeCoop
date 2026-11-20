---
id: BUG-9291
title: Era visitors: another era's vehicles and buildings drawn as props are not reliably visible
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5 (Civ era visitors)
role: any
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 39901e3b
tests: in game: TITAN QA runs (coop.visit at / move / status screen positions); tools/tests/galaxy_server_check.py (the stream)
signature:
docs: docs/12-server-and-players.md#server-era-visitors-more
related: BUG-9290
---

## Report
Another era's vehicles and buildings are made as ground props at the right places (status: alive, on the
ground) but mostly do not show. A truck prop showed once (a fresh game at 1280x960, near the nest, as in the spike of
2026-11-17), and not after the avatar was moved, nor in any 800x600 game, nor after a hot reload.

## Root cause
Two things. (1) The tests did not look at the props: the spike's "25 m ahead of the avatar" is not where the
avatar faces, and `coop.nest look` leaves its point off the screen too (the status now prints each prop's screen
position: x -193, -141, -257 for "invisible" ones). (2) Real: an ornament prop draws only near the place it had in its
first frames. Made and placed in one frame anywhere, it draws (a truck at the screen's centre, 800x600, switch-free);
moved more than its size later (`coop.visit move`), it is no longer drawn, even with its origin at the screen's centre
(x 399, y 394, 4.6 m from the creature); SetModelKey again after the move did not bring it back. The bake was not the
cause (all levels baked; asked for anyway). Two flag bytes that differ from a real vehicle (+0x71, +0x74 of its spatial
part) changed nothing.

## Fix
A streamed object's prop is made at its place and placed in the same frame; a moving one (a vehicle) is made
again when it is more than 6 m from where its prop was made (switch `galaxy.prop_remake`). Test commands:
`coop.visit at <x y z>` (the next spawn there), `coop.visit move <x y z>`, screen positions in `coop.visit status`.

## Verification
TITAN QA runs, 2026-11-20:
- FAIL (moved after it was made: `coop.visit move` to the screen's centre): not drawn. PASS (made there:
  `coop.visit at` then spawn): the truck drawn (800x600).
- The Civ stream: QA 9 on Abber (Creature), QA 8 hosting Abber (Civ): the city's buildings drawn over the Creature
  landscape (images/stage4_civ_visitors.png), their props at screen positions 344..684; a vehicle the Civ game ordered
  to drive was made again 5 times along its way (`galaxy.prop_remake` off: once, and left behind).

## History
- 2026-11-20: opened.
- 2026-11-20: opened with the runs that rule things out.
- 2026-11-20: found (the tests did not look at the props; an ornament draws only near where it was made) and fixed.
