---
id: BUG-0839
title: game::world_epoch() counts only Creature worlds: Tribe users of the epoch cannot tell two Tribe worlds apart
status: not-reproduced
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch: fix/w48-tribe-civ
commits:
tests: tribe_member_age_rejoin tribe_pens
signature:
docs: 
related: BUG-0119 BUG-0427 BUG-0384
---

## Report
No player report. Code audit finding outside Creature: anything that uses game::world_epoch() in Tribe (the deferred destroy's world token, the nest egg mirrors in Tribe) sees one world across a Tribe load or a rejoin's world.

## Root cause
Confirmed in the code, no harm found: game::world_epoch() still counts Creature worlds only (game.cpp), but every
user of it that runs in Tribe has another guard for a new Tribe world:
* The per-world state of npc.cpp, nest.cpp (the egg mirrors), fields.cpp and worldobj.cpp is dropped by
  `avatar_change_check` (coop.cpp): the Tribe adapter's local entity (modes.cpp rts_local, the marker, null while
  the planet's anchor is missing) changes to null at every Tribe load or rejoin, which calls their on_world_change
  (nest's drops its mirrors without touching them). nest's and worldobj's own epoch compares are a second check.
* The deferred destroy queue (game.cpp destroy_deferred_tick): its world token cannot tell two Tribe worlds apart,
  but since BUG-0825 (`npc.defer_identity`) every queued entry is dropped in every frame when its object is not the
  listed creature with the serial it had when queued: a creature of the old world is freed with it, and a new one at
  its address has another serial. Only a creature at the same address with the same serial could pass both, which a
  new world does not make (serials count up per game run).
* The listed-object cache (game.cpp `listed`) is keyed by the frame index first: a new world is always a new frame.
* npc.cpp's per-world settle (1 s before the first proxies) is the only user without a reset: after a Tribe reload
  the guest's proxies may come 1 s earlier; tribe_rejoin_npcs (BUG-0328) covers the rejoin's NPCs.
Runtime: the guest's Tribe world was loaded again (a rejoin) in lab 20261112-090456-2, 20261112-090653 (tribe_member_age_rejoin)
and 20261112-091646 (tribe_pens): no fault, no divergence after the rejoin.

## Fix
None: no Tribe user of world_epoch is harmed (Root cause). world_epoch stays a Creature counter; stage code uses stage_epoch() or its own reset.

## Fix idea
Use game::stage_epoch(mode) for the world token and in nest.cpp, or make world_epoch count every stage.

## How to test
Tribe: queue a deferred destroy during a cinematic, load the world again before it ends; the queued pointer must be dropped as 'another world' (fix) vs destroyed in the new world (main).

## Verification
no-fail-run: no consequence reachable in Tribe: every Tribe user of world_epoch has a reset of its own or the
serial check (Root cause). Runs with a Tribe world loaded again on the guest: lab 20261112-090456-2, 20261112-090653,
20261112-091646. The ticket's test (a deferred destroy queued during a cinematic, the world loaded again before it
ends) cannot fail while npc.defer_identity is on; with it off it needs a new creature at the queued block in the new
world (as coop_defer_identity.py forces in Creature); not run.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-tribe-civ): not reproduced: every Tribe user of world_epoch has its own world reset or the serial check.
