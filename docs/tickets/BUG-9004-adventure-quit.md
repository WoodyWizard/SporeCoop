---
id: BUG-9004
title: The campaign save-and-quit command crashes in a standalone adventure
status: fixed
kind: bug
stage: Space
reported: 2026-11-03
source: Duo fork Windows lab
role: both
versions: 0.7.3.6-duo5, 0.7.3.6-duo6
fixed_in: 0.7.3.6-duo6
branch: duo-species
commits:
tests: tools/tests/duo_adventure.py
signature: cStarManager::GetPlanetRecord+0x2D
docs: docs/re/space.md#space-duo-adventure
related: BUG-9003
---

Run Adventure Town from the galaxy menu, then enter the developer command
`coop.menu`. On Duo 5 the disconnected guest crashed. On Duo 6 both games
crashed after leaving their session and using the command. The game's campaign
save path required a home planet and read a null pointer. This test did not
use the game's own Quit Adventure button.

Duo 6 refuses campaign saves and the campaign quit message during Scenario mode.
The two-instance test checked both refusals on each game. Both then used Options >
Exit to Main Menu and returned to GalaxyGameEntry without a fault.
