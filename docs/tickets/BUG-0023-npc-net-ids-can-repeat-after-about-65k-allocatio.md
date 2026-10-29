---
id: BUG-0023
title: NPC net ids can repeat after about 65k allocations in one world
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: code review (tools comparison)
role: both
versions: all
fixed_in: 0.7.3.0
branch: fix/w2-creature
commits: 8de7ca7
tests: npc_reuse, unit
signature: 
docs: docs/re/creature.md#creature-pool-ids
related:
switch: npc.id
---

## Report
npc.cpp host_tick allocates ids with g_next_id++ and wraps without checking g_host_by_id; creatures unseen for 2 s get new ids when they return.

## Root cause
Latent, not seen at runtime.

## Fix
Fixed (fix/w2-creature): `alloc_npc_id` (mods/coop/npc_ids.h) gives the next id in 1..0xFFFE that
`g_host_by_id` does not hold (0 when all are in use: the creature is not shared that frame). This
stays in the NPC code; the ids of ships, vehicles and tribe members (BUG-0154) are the net branch's.
`coop.npc idfix off` keeps the old wrap (tests).

## Verification
* Unit test `tests/npc_tests.cpp` (`make test`): 5 NPCs that stay and 200 000 that come and go;
  with the old wrap (`skip_used` false) ids of the five are given again (checked > 0, the bug), with
  the fix never; edges (0xFFFE then 1, all ids used, a run of used ids).
* Two-game test `coop_npc_reuse.py` part 2 (`coop.npc nextid <id in use>` then `coop.npc epic 0 8`):
  FAIL without the fix (`coop.npc idfix off`, lane 5, 2026-10-28: id 1 twice in the host's list),
  PASS with it (direct run, lab run out/lab/20261027-232407).

## History
- 2026-10-25: reported (code review (tools comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fixed on fix/w2-creature (allocator skips ids in use); unit test and npc_reuse part 2.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: npc.id (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
