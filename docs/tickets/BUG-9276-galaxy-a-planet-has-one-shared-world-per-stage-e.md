---
id: BUG-9276
title: Galaxy: a planet has one shared world per stage (eras); old saves kept, a wrong stage or another planet's save refused
status: fixed
kind: feature
stage: all
reported: 2026-11-19
source: owner 2026-11-19 (7C)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 6a73ae46
tests: tools/tests/galaxy_server_check.py
signature:
docs: docs/12-server-and-players.md#server-eras-built
related: BUG-9277, BUG-9273
---

## Report
Owner 2026-11-19 (7C): players in different stages are on the same planet, each stage a shared world ("they should be
in the same stage with other players in their stages"). Step 4 part 1 and part 6
(docs/12-server-and-players.md#server-party-progress-design).

## Root cause
The galaxy server kept one session and one stored world per planet name: a host whose game went on to the next stage
would have stored its Tribe world as the planet, ending the planet's Creature stage for everybody on it. Also any
host could store any save over a planet (S6).

## Fix
* Eras: a planet session is keyed "<planet>|<Era>" (net/galaxy.h era_key, era_of, era_planet, era_display); hosting,
  storage, hand-overs and joins work per era unchanged. Protocol 7: an upload names its era; the server refuses a
  world of another stage than the era's. A plain name typed in `coop.galaxy enter` gets the era of the stage the game
  plays, or in the menu the planet's only era. Messages and the galaxy menu show "Samuth (Creature)".
* Worlds stored before eras move to their stage's era at the server's start (the tech level; 0 counts as Cell).
* Planet saves stay safe: the server keeps the 3 saves before the current one (<file>.1 .. .3) and refuses a save
  of another planet over a stored one (the save's own name inside the world).
* The server's switch is the protocol: there is no switch for the eras (an older game is refused at HELLO).

## Verification
no-fail-run: new feature (the server checks cover the new rules: a Tribe world refused by a Creature era, another
planet's save refused, the older save kept, a Tribe era its own session; 24 checks pass).
Live, TITAN 2026-11-19, QA 8 and 9 on the test galaxy worlds_6ga: the four stored worlds became Samuth|Creature,
Clyth|Tribe, Jiff Peanutbutter|Tribe, Cellfour|Cell; QA 8 entered "Samuth" from the menu and hosted Samuth (Creature),
QA 9 joined it; two uploads left Samuth_Creature-*.world.1 and .2 beside the world.

## History
- 2026-11-19: opened and built on feat/lineages-alpha.
