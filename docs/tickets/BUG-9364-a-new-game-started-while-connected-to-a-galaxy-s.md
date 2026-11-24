---
id: BUG-9364
title: A new game started while connected to a galaxy stays off the galaxy
status: fixed
kind: bug
stage: Galaxy
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 0d0214dc
tests: tools/qa_win (manual, TITAN + the MSI, TITAN's galaxy server): a new Cell game from an empty star while connected
signature:
docs: docs/12-server-and-players.md#server-new-game-planet
related:
---

## Report
Deep playtest 2026-11-24: a player connected to the galaxy server who clicked an empty star and went through
Spore's own new game wizard (Cell) played that game off the galaxy: `coop.galaxy status` planet='' role=-,
`coop.status` offline, the server listed no new planet, nobody could see or join it. Only the Social Menu's "Private
planet: New" or the console's `coop.galaxy enter` put a new world on the server. The owner's idea of the galaxy menu
(2026-11-17, docs/12-server-and-players.md#server-galaxy-menu: "'New game' makes a new planet on the server") was
never built for a public planet.

## Root cause
Observed (code): galaxy_client.cpp private_tick entered a new world only while the private row waited
(g_private_pending); galaxy::enter had no other caller for a world this game made itself (the menu's Play enters a
planet the server already has).

## Fix
galaxy_client.cpp private_tick: a world this player starts while connected (a stage's world, its name not the one
played at the connect, on a planet or in a session before) enters the galaxy as a new planet once it settled for
5 s, as the private one does (without the private flag). One try per world. A name the galaxy already has stays off
the galaxy (entering it would join that planet and drop the new world): the player is told to start one with another
name. `coop.galaxy status` has a "new game planets" line. Switch `coop.fix galaxy.new_game_planet off`: only the
private row and the console.

## Verification
- FAIL (build ffd01832, 14:13): a new Cell game 'Medonra' from an empty star, connected: planet='' role=-, the
  server's status "0 planet(s)" besides the two stored ones, `coop.status` offline.
- PASS (the fix, 14:19): a new Cell game 'Talas': "galaxy: the new game 'Talas|Cell' becomes a planet of the galaxy
  (BUG-9364)", "You host Talas (Cell) for the galaxy."; the server: "planet 'Talas|Cell': hosted by 3"; the MSI's
  galaxy menu showed Talas, its Play downloaded the world (3332 KB) and joined as a guest. The planet then carried the
  whole playthrough (Cell, Creature, Tribe, Civ eras).
- Not run: the name already taken on the server (the refusal's text), a new game started with the switch off on the
  fixed build.
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the run of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
