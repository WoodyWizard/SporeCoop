---
id: BUG-9278
title: Galaxy planet: a party that ends its stage moves on into the planet's next era (the others keep theirs)
status: fixed
kind: feature
stage: all
reported: 2026-11-19
source: owner 2026-11-19 (step 4)
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 4b8076f7 b23e9a9b bcebb401 ea7e2a87 50bc3c2f ecc9de53 3a747dda 9705efe3
tests: tools/tests/galaxy_server_check.py
signature:
docs: docs/12-server-and-players.md#server-party-stage-end
related: BUG-9276, BUG-9277
---

## Report
Step 4 part 3 (owner 2026-11-19 on 6Y and 7C): a party that ends its stage moves on into the planet's next era and
joins the players there; the other players keep the era they are in.

## Root cause
The stage end ran for the whole planet session: the host's game took every player of the planet into the next stage.

## Fix
docs/12-server-and-players.md#server-party-stage-end: the leader's MOVEON splits its party into a crossing session
(warm, no reload), the co-op stage end runs there for the party alone, ARRIVE makes the crossing the next era or joins
the era's host. Switch `galaxy.party_stage_end`. Only the Creature stage's end so far.

## Verification
Server: tools/tests/galaxy_server_check.py (11 checks: split, the era handed to a player who stays, crossings never
listed, entered or stored, ARRIVE renamed and joined). Live (TITAN 2026-11-19): the split and the warm crossing host
seen; then the whole crossing (TITAN 2026-11-19 22:24): the host's party crossed, played the native Tribe start
and arrived as Samuth (Tribe), stored by the server, while the other party kept Samuth (Creature). Not in game yet: a
party of 2+, a guest's party crossing whole, arriving where the next era has a host.

## History
- 2026-11-19: opened; server and game sides built on feat/lineages-alpha; the whole crossing seen in game.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
