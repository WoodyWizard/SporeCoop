---
id: BUG-9288
title: Galaxy server: visitors only reach players near them (interest radius)
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5 (MMO-sized planets)
role: any
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: e2e7c598
tests: tools/tests/galaxy_server_check.py
signature:
docs: docs/12-server-and-players.md#server-era-visitors-more
related: BUG-9287
---

## Report
Every VISIT went to every player of the planet's other worlds: on a big planet each game would get everyone.

## Root cause
Feature (interest management).

## Fix
The server keeps each player's last VISIT position and passes a VISIT only to players within --visit-radius (300 m; 0: everyone); a player that sent none (a menu, the Civ stage) gets them all.

## Verification
- FAIL (`GALAXY_CHECK_ARGS="--visit-radius 0"`): "a VISIT does not reach a player 400 m away" fails.
- PASS (default): 400 m away gets none, 100 m away gets it (tools/tests/galaxy_server_check.py).

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game / in the server checks (TITAN).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
