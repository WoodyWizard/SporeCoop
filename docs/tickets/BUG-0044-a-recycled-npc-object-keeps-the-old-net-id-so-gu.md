---
id: BUG-0044
title: a recycled NPC object keeps the old net id, so guests see the wrong species or a walking corpse
status: fixed
kind: desync
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#22
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-creature
commits: 8de7ca7
tests: npc_reuse
signature:
docs: docs/re/creature.md#creature-pool-ids
related: #22
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/22 (full text, discussion).

The host gives NPC net ids by creature object pointer and keeps the mapping for 2 s after the creature last updated. Creature objects are pooled. When the game frees an NPC and builds a new one in the same object within that time, the new NPC inherits the old id and no new spawn goes to the guests. Guests keep showing the old species, and if the old NPC was dead, a corpse dragged along the path of a living creature. Severity: wrong behaviour (desync of what the players see).

### Where
- Id by pointer, reused while `last_seen` is younger than 2 s: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L231-L257
- Guest: `px.killed` is only reset by a new MSG_NPC_SPAWN or a world change: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L461-L482
- The species is sent only in the spawn: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L179-L200

### How to reproduce
1. Two instances, Creature stage, NPC sync on (default).
2. Host and guest stand near a nest. The host kills an NPC and eats the corpse until it is gone, then waits for the nest to refill. Or the host runs far away and back to make the game unload and recreate NPCs.
3. On the host, `coop.npc list` shows a pointer with the same `#id` but another species; on the guest, `coop.npc list` shows the proxy with the old species and ` DEAD`.
4. Faster: a test command that destroys a host NPC (`DestroyInstance`) and spawns another species next to it in the same frame, then checks whether the new object got the freed pointer.

### Expected
A new creature always gets a new net id (or at least a fresh spawn), and the guest's proxy shows the species and the alive state of the host's creature.

## Root cause
**What happens**

1. `host_tick` loops over the creatures that ran `Update` this frame. For a pointer already in `g_host_npcs` it only refreshes `last_seen`. A pointer is forgotten (despawn to guests) only after 2 s without an update.
2. Creature objects are pooled: `cCreatureAnimal::Create(pos, species, age, herd, isAvatar, reuse)` has a reuse flag, and `npc::creatures()` lists "creature objects without a species: not in the world (pool)".
3. If NPC X is freed (eaten corpse, unloaded when the avatar runs off, herd cleanup) and the game creates NPC Y in X's object within 2 s, `g_host_npcs.find(Y)` hits X's entry: same id, no `send_spawn`, so the species key and `max_health` are never sent again.
4. On the guest, the proxy for that id keeps X's species. If X died, `px.killed` stays true forever, because the state path never clears it:
   ```cpp
   if (!px.killed)
       drive::step(...);
   else { // a corpse lies where the host has it
       ...
       game::teleport(px.c, s.pos, s.rot);
   }
   ```
   So the guest sees X's corpse teleported along Y's living path, with no health or impressed updates.

### Confidence
PLAUSIBLE. The code path is CONFIRMED by reading. Not verified: how often the game hands out a freed creature object again within 2 s (pool order, timing).

## Fix
Fixed (fix/w2-creature): `HostNpc` keeps the species instance and the death flag the object had.
In `host_tick` a known object that now has another species, or is alive after it was dead
(`npc_object_reused`, mods/coop/npc_ids.h), is retired: despawn to every guest that mirrors it,
its id and AI target forgotten, its joint-hunt hits dropped; then it gets a new id and a fresh
spawn with its species. `coop.npc reusefix off` keeps the old behaviour (tests). Guest side
unchanged (a new id is a new proxy). Not caught: a living NPC freed and rebuilt alive in the same
object with the same species (the guest then shows that creature with the old one's health for a
moment: harmless). docs/re/creature.md#creature-pool-ids: Create's reuse flag takes pool objects
of the same species, so "dead, then alive" is the main case.

## Verification
Test `tools/tests/coop_npc_reuse.py` (suite `npc_reuse`), part 1, a test command that forces the
condition: a natural reuse could not be forced (DestroyInstance on a corpse, then Create with the
reuse flag of its species for 8 frames never returned the object, lane 5), so `coop.npc reuse <id>`
makes the dead NPC's object alive again (death flag cleared, full health): what host_tick meets
after the game rebuilt it. The assertion is the guest's game: the mirror of the host's id of that
object, its species and death flag in `coop.npc creatures`.

* FAIL without the fix (`--without-fix` = `coop.npc reusefix off`, lane 5, 2026-10-28, direct run):
  host object 0FA923A8 alive with the same id 19; the guest's mirror of #19 is still DEAD in its game.
* PASS with the fix: direct run (lane 5): the object got id 22, the guest's mirror of #22 is alive
  and the same species; lab run out/lab/20261027-232407 (`npc_reuse`).

### Related
- BUG-0023 (net ids repeat after about 65k allocations): same allocator, a different cause.
- docs/re/creature.md 11 (NPC ids across worlds), 14.1 (how a creature dies).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #22.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-creature (host_tick retires a reused object's id); test npc_reuse FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
