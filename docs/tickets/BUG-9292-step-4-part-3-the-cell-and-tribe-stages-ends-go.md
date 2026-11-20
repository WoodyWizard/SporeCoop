---
id: BUG-9292
title: Step 4 part 3: the Cell and Tribe stages' ends go through the party's crossing too
status: fixed
kind: feature
stage: cell
reported: 2026-11-20
source: step 4 part 3 (docs/12 #server-party-stage-end)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 4b8076f7
tests: tools/tests/galaxy_server_check.py (the server side, BUG-9278); in game: TITAN QA runs
signature:
docs: docs/12-server-and-players.md#server-party-stage-end-more
related: BUG-9278, BUG-0315
---

## Report
Only the Creature stage's end went through the party's crossing (BUG-9278). On a galaxy era the Cell -> Creature
switch and the Tribe -> Civ graduation kept the session-wide way: the game played the next stage while the server
still listed it on the old era.

## Root cause
Feature: the routing was built for the Creature stage-end request only.

## Fix
The Cell stage's switch (h_CellToCreature, History's check) and the Tribe stage's native graduation request
(h_Submit, native_tribal_graduation) ask galaxy::stage_end_request first, as the Creature stage end does: the party's
leader moves the party into its crossing, the switch / request is held and runs again there (cross_tick); a member who
is not the leader is told so. Switch `galaxy.party_stage_end_more`. Test command `coop.vote creature` (Cell: what
History's check does).

## Verification
TITAN QA 9 hosting the test galaxy's Cellfour (Cell) world, 2026-11-20:
- FAIL (switch off): `coop.vote creature`: the game went to Creature while the server listed it on "Cellfour|Cell".
- PASS (on): held, the party moved into "Cellfour~1|Cell" (5 s), the switch ran there, the game played Creature and
  on arrival the crossing became "Cellfour|Creature" (15 s); the first creature's editor opened.
- Tribe -> Civ: built the same way, not run (it needs a Tribe save at its graduation: ~40 minutes of scripted raids,
  docs/06-testing.md).

## History
- 2026-11-20: opened.
- 2026-11-20: built; Cell -> Creature seen in game, Tribe -> Civ not run.
