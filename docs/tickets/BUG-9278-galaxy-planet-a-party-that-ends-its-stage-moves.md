---
id: BUG-9278
title: Galaxy planet: a party that ends its stage moves on into the planet's next era (the others keep theirs)
status: open
kind: feature
stage: all
reported: 2026-11-19
source: owner 2026-11-19 (step 4)
role: host
versions: 
fixed_in:
branch: feat/lineages-alpha
commits:
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
seen; the native Tribe start in a crossing and ARRIVE not seen yet (needs a test save at the last brain level).

## History
- 2026-11-19: opened; server and game sides built on feat/lineages-alpha (open: the whole crossing in game).
