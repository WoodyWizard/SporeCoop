---
id: BUG-1182
title: Creature: creature_mating and editor_now fail on rel/0.7.15 96e0fd4c: the host's game picks no mate near the nest (coop.drama mate 00000000)
status: open
kind: bug
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-194833, 20261115-202054
role: 
versions: rel/0.7.15 96e0fd4c, feat/w51-npc
fixed_in:
branch: 
commits:
tests: creature_mating, editor_now
signature:
docs: 
related: BUG-1141, BUG-1142, BUG-0516
---

## Report
creature_mating and editor_now fail at their first step on feat/w51-npc's final tree (20261115-194833 lane 2) and on
rel/0.7.15 96e0fd4c (creature_mating, 20261115-202054 lane 3): "the host's game picked a mate near the nest", "the mate
is one of the host's NPCs (None)" (`coop.drama mate`: `mate 00000000 flags=0`). npc_editor_hold hit the same once
(20261115-193906) and opens the editor through `coop.vote evolve` since.

## Root cause
Not analysed. Not from feat/w51-npc (rel/0.7.15 96e0fd4c fails the same). Pending: whether 9362bcdd (before
feat/w51-core2) passes.

## Fix
None yet.

## Verification
no-fail-run: open.

## History
- 2026-11-15: opened (feat/w51-npc).
