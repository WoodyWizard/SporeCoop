---
id: BUG-9287
title: Step 4 part 5: visitors across eras (players of a planet's other eras shown look-only)
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5, decisions 5S A / 5T B (docs/12 #server-era-visitors)
role: any
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/tests/galaxy_server_check.py (server side); in game: TITAN QA runs (docs, Verification)
signature:
docs: docs/12-server-and-players.md#server-era-visitors-built
related: BUG-9286, BUG-0360
---

## Report
Step 4 part 5: players of a planet's other eras (its Creature and Tribe worlds, a party's crossing) should see
each other, look-only (owner 2026-11-17: "Seeing civilizations as a creature would be an amazing feat").

## Root cause
Feature. Eras are separate sessions, so nothing of one reached the other; and another era's player's creature is
mostly a creation the other game does not have. A creature on the spike's ground prop draws nothing.

## Fix
VISIT / VISITOR on the galaxy server (each player's species and position, passed to the planet's other worlds);
era_visitors.cpp shows them as puppets with no AI that walk there; galaxy_crt.cpp and the server's creation store
(CRTPUT / CRTGET) bring a creature the game lacks. Switch `galaxy.era_visitors`; server `--no-visitors`.

## Verification
TITAN QA runs (no lab on Windows), docs/12-server-and-players.md#server-era-visitors-built:
- FAIL (`galaxy.era_visitors` off on one game): both games show no visitor. PASS (on): each shows the other era's
  player (images/stage4_era_visitors.png); it walks with its player.
- FAIL (before the creation store, the same games): both visitors not shown (no species, 1583 / 1556 tries).
  PASS: each game got the other's creature from the store (one ask each) and showed it.
- tools/tests/galaxy_server_check.py: 4 VISIT checks, 5 creation store checks.

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game (TITAN, two QA games).
