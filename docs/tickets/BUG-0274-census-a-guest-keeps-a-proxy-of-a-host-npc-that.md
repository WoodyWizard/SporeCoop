---
id: BUG-0274
title: census: a guest keeps a proxy of a host NPC that the host has at (0 0 0), 504 m apart (Creature)
status: fixed
kind: desync
stage: creature
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w7-npc
commits: 5449631
tests: tools/tests/coop_npc_pooled.py (suite npc_pooled)
signature:
docs: docs/re/creature.md#creature-npc-pool-cull, docs/06-testing.md#testing-npc-pooled
related: BUG-0280, BUG-0044
---

## Report
campaign_short#2 and #3 of lab 20261029-122937 (out/lane2/campaign/20261029-135237 and
out/lane3/campaign/20261029-135721): census finding "c90: 504 m apart (creature)" (and c62 on Dave).
The host lists `c90 key=290D67AD alive=1 hp=10 ai=1 own=npc pos=(0.0 0.0 0.0)`; the guest has
`c90 key=7D5730C8 own=proxy pos=(136.0 -473.6 -106.5)` on the surface. A host NPC at the planet's
centre (not placed yet? removed without the alive flag?) whose proxy stays on the guest. 504 m both
times. Census files: 01_census.txt in those run folders.

## Root cause
Observed (lane 4, 2026-10-29): the object is a real NPC of the host that the game had just put
back into its creature pool. The creature stage keeps NPCs only around the host's own avatar; one
farther away (about 150-200 m) stays in the noun list and `object_alive`, keeps its species and
health, but gets herd 00000000, position (0 0 0) and no more Update calls (`coop.npc creatures`).
`host_tick` forgot a tracked NPC only after 2 s without an update, so for 2 s the host still listed
it with its net id at the planet's centre while the guest's proxy stood where it was last sent
(no despawn, no states; the guest's stale check needs 5 s). In the campaign runs the host was 190
and 300 m away from its nest and a guest stood at it: the nest spawns a missing family member again
and the game pools it again 1-3 s later (cycle 3-6 s, measured with the host 300 m away), so the
window repeats all the time and two checks 30 s apart could both hit it. hp=10 and the host's own
species: a family member of the nest (the nest at about (140 -473 -104) in both runs, the guest's
proxy there).

Which side was wrong: the host's NPC sync (it kept an id for an object that was no longer in the
world). The host's position read is right (the game's object really is at (0 0 0)). The census
(`census.py`, `campaign_lib.census_problems`) needed no change: it reported a real 2 s ghost on the
guest; with the fix the pooled object has no id (`own=local`) and is not paired.

What is left is the game's own behaviour (NPCs only around the host): BUG-0280.

## Fix
`npc.cpp retire_pooled()` (from `host_tick`, only on frames where some tracked NPC did not update):
one walk of the noun list; a tracked NPC that did not update and is not listed (freed) or is listed
without a herd or at (0 0 0) (pooled) is retired at once (despawn to every guest that mirrors it).
Fields are read only of listed objects. `coop.npc list` shows `pooled=` (count). Switch:
`coop.fix npc.pooled off` (`SPORE_FIX_OFF=npc.pooled`) keeps the 2 s grace.

## Verification
Test `tools/tests/coop_npc_pooled.py` (suite `npc_pooled`): the guest stays at the nest, the host
walks 300 m away and waits 30 s; every 0.6 s each guest proxy must match a host NPC of the same id
that is in the world (the game's own objects of both machines, `coop.census`).

* FAIL without the fix (`SPORE_FIX_OFF=npc.pooled`, lane 4): out/lab/20261029-144839, 13 ghosts
  (host (0 0 0), guest proxy on the surface); direct run on lane 4 with `--without-fix`: 12 ghosts,
  72 host NPC rows at the planet's centre in 177 samples.
* PASS with the fix: out/lab/20261029-145028 (`npc_pooled` 2/2, with `npc_reuse` and
  `cinematic_puppet` 2/2 each), direct run on lane 4: 0 ghosts, 0 rows at the centre.
* Also: smoke out/lab/20261029-145543 (14/14), campaign_short out/lab/20261029-151043 (OK, no
  census finding; one run only, the bug showed in 2 of 3 before).

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w7-npc (cause: the NPC was back in the game's creature pool; BUG-0280 opened for the culling itself).
- 2026-10-29: released in 0.7.3.1.
