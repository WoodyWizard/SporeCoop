---
id: BUG-9350
title: Galaxy: Spore's Choose A Stage ring stays on screen into the planet when a join or load starts while it is open
status: open
kind: bug
stage: civ
reported: 2026-11-23
source: QA (MSI)
role: guest
versions: 0.7.14.1-lineages69-dev
fixed_in:
branch: feat/lineages-alpha
commits:
tests:
signature:
docs: docs/12-server-and-players.md#server-space-ships
related: BUG-9337, BUG-9335
---

## Report
Cross-PC QA (2026-11-23, TITAN's galaxy server, two test games on the MSI). A game that had joined a Space era's host
(the guest's "Civilization victory" ship-design path) went back with `coop.menu` (still the host's guest in the galaxy
menu) and pressed Play on the same planet: Spore's Choose A Stage ring opened (log "Spore's Choose A Stage screen on
'Abber': Space, Civ (BUG-9337)"), and at once the guest's rejoin card ("Joining MSISpace, MSISpace is saving the
world...") came over it. The card ended by its 180 s time limit. `coop.galaxy leave` and `coop.galaxy enter Abber|Civ`
then loaded Abber's Civ era from the menu: the ring (paw, fire, globe, rocket and its X) stayed drawn in the middle of
the Civ game (images/bug9350_stage_ring_over_civ.png), while `coop.gmenu status` said "closed 1, open now 0". A click on
its X closed it.

## Root cause
Inferred from galaxy_menu.cpp: tick() marks the screen closed when the game leaves the menu (mode not the menu, or
the menu's state not 4) but does not close Spore's screen itself; only a stage press (h_FlowButton: the flow's Back)
or the player's own Back closes it. A load that starts while the screen is open (a rejoin, a party move, a console
enter) leaves the menu with the screen's window still up, and the planet draws it.

## Fix
Not fixed yet. Candidate: keep the flow from h_FlowOpen and press its Back (as a stage press does) before our code
starts a planet load from the galaxy menu, and when tick() sees the menu left; behind a switch.

## Verification
Not fixed yet.

## History
- 2026-11-23: opened (seen on the MSI in a cross-PC run).
