---
id: BUG-0130
title: a guest in another world gets the host's DNA, NPCs, tribe and cells applied to its own game, which is then saved
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#109
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: 7e2173d
tests: tools/tests/coop_join_other_world.py (suite join_other_world)
signature:
docs: 
related: #109
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/109 (full text, discussion).

Most stage modules apply the host's state to a guest as soon as the guest is in the same game mode. Nothing checks that both play the same world. A guest that plays another world of that stage gets its own game changed: DNA set to the host's total, its NPCs destroyed and the host's spawned, its tribe members replaced, its cells purged and its food total overwritten. The quit message that then loads the host's world saves the changed game. Severity: medium-high. With `auto_load_world=0` it is high: the guest never leaves its own world, so the changes go on, and the next save writes them.

### Where
- Host, NPC relevance: `ctx.player_pos` takes the guest's position with no `same_place` check: [coop.cpp#L2654-L2663](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2654-L2663). Compare squadron's `with_host`, which checks it: [coop.cpp#L2886-L2889](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2886-L2889), and `partners()`: [coop.cpp#L2279-L2291](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2279-L2291).
- Host, NPC sending: in the tribe stage every guest gets the NPCs around the host's tribe; otherwise the NPCs near the guest's own coordinates: [npc.cpp#L261-L275](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L261-L275).
- Guest, NPCs: the update filter destroys every local NPC while connected in Creature or Tribe (only `host_away` stops it): [npc.cpp#L93-L106](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L93-L106), [npc.cpp#L579-L600](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L579-L600).
- Guest, tribe: checks the mode only, then binds or spawns members and removes every member the host does not know: [tribe.cpp#L1110-L1111](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1110-L1111), [tribe.cpp#L668-L687](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L668-L687).
- Guest, cell: `in_cell()` only, then the purge of local cells and the food total: [cellsync.cpp#L1209-L1212](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1209-L1212), [cellsync.cpp#L1067-L1078](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1067-L1078), [cellsync.cpp#L1316-L1332](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1316-L1332).
- Guest, DNA and time of day: [coop.cpp#L1543-L1566](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1543-L1566).
- Guest, species relations: [social.cpp#L117-L135](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L117-L135).
- The load that follows leaves the stage with the quit message, which saves the game: [world.cpp#L550-L562](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L550-L562), docs/re/space.md 8.2.

### How to reproduce
Cell, 2 players:
1. Host: start or load a Cell game and host.
2. Guest: load a different Cell save of its own (another planet). Note its food total and the cells around it.
3. Guest: press F9 and join the host while staying in that game.
4. Within a second the guest's food total jumps to the host's (a level change if the totals are on different sides of a level line). The guest's own NPC cells are removed (`coop.pool`, `purged`).
5. Let the host's world arrive. The guest goes to the menu (this saves). Leave the session and load the guest's own save again: the changed food and level are in it.

Creature: the same with two Creature saves. The guest's DNA becomes the host's total, and the NPCs around the guest disappear (`coop.npc`: `local_removed` grows).

Tribe: the guest's own tribe members are replaced by members spawned where the host's members stand.

With `auto_load_world=0` in the guest's coop.ini this continues for as long as the guest stays.

### Expected
A guest whose own state (latest host `PlayerState` mode and world) does not match the host's applies nothing from the host's world: no DNA, food, cells, NPCs, tribe members or relations. The host sends no NPCs to it. Its own game stays as it was until the host's world is loaded.

## Root cause
**What happens**

1. `same_place(s)` (mode, world and `g_world != 0`) exists, and `partners()` and squadron use it. `npc::Ctx::player_pos`, `tribe::on_message`, `cellsync::on_message`, `MSG_DNA`, `MSG_WORLD_TIME` and `social::enforce_table` do not. They check only the local mode (or that an avatar exists).
2. The guest can be in another world of the host's stage while connected:
   - a: the player presses F9 inside their own single-player game and joins (the window opens anywhere);
   - b: the guest loads one of its own saves while connected;
   - c: the host loads another save of the same stage (`follow_phase` `other_world`: at least 5 s plus the share save);
   - d: just after a stage advance (host in Tribe, guest still in Creature for about 15 s).
3. Meanwhile the host sends `MSG_DNA` (resent at every join, `g_dna_sent = -1`), `MSG_CELL_FOOD`, cell, tribe and NPC snapshots, and the species table. On the guest:
   - `game::dna_add_raw(total - dna())`;
   - `update_filter` destroys its NPCs;
   - NPC proxies (and herds made for them) appear at coordinates from the other planet;
   - `guest_state` (tribe) spawns members at the host's positions and removes the guest's own;
   - `guest_state` (cell) purges local cells, and `o_AddFood(diff)` levels the cell up or down;
   - `enforce_table` writes the host's relations.
4. `world::finish` -> `load_slot` -> `post_quit_to_menu` posts 0x4715068, and the game **saves** before going to the menu. In case a (and b), that save is the player's own single-player game. In case c it is only the co-op copy slot. With `auto_load_world=0` nothing leaves the world, so steps 3 and 4 go on until the player saves.

### Confidence
CONFIRMED by reading. A second reviewer verified it independently. Not checked in a game: that the quit message's save writes exactly these changes (the save itself is ✔ in docs/re/space.md 8.2).

## Fix
Fixed on fix/w2-world (7e2173d). coop.cpp `host_elsewhere()`: the host's latest state is in the
guest's stage mode but another world. Then the guest drops the host's world messages at the
dispatch (`MSG_DNA`, `MSG_WORLD_TIME`, every cell message incl. food, tribe state/commands,
species table and social); the DNA total is kept and applied once the guest plays the host's world
(`host_here()`), also when it arrived before the host's first state. npc's `host_away` is true
while the host is elsewhere (also before its first state): the guest's own NPCs are not removed.
The host sends NPCs only to players in its place (npc `Ctx::in_place`, the RTS branch too): a
guest that leaves the place gets its proxies despawned and receives them again when it is back.
Old behaviour: `coop.corefix same_place 0`.

Not covered: a host table of relations received while the guest was in the host's world is still
enforced by `social::enforce_table` if the guest then loads a world of its own while connected
(case b); civ, space and the shared money are other modules' (not in this ticket's list).

The review suggested:

- Add a `host_here()`-like check (`same_place(latest host state)` and `PF_IN_WORLD`) to the context of npc, tribe, cellsync and social, and gate every guest-side apply on it: snapshots, `update_filter`, `MSG_CELL_FOOD`, `MSG_CELL_HIT`, `MSG_DNA`, `MSG_WORLD_TIME`, `enforce_table`.
- On the host, make `player_pos` return false unless `same_place(guest state)`, as `with_host` does for squadron. The rts branch should also skip guests that are not in the host's place.
- Keep the old behaviour behind `coop.fix same_place off`.

## Verification
Two-game test `coop_join_other_world.py` (Creature fixture): the guest leaves, loads a world of its
own (the fixture planet copied as Otherworld.spo: another world id, its own NPCs), keeps it
(`coop.worldfix autoload 0`) and joins; the host earns 50 DNA. FAIL with `--without-fix`: the
guest's DNA went 0 -> 50 and 22 of its own NPCs were removed; PASS (lab run 20261027-231952 and the
smoke run): DNA stays 0, no NPC removed, still in Otherworld.

The planned test was:

`coop_join_from_own_game.py` (two instances, Cell and Creature fixtures with two different saves, guest `auto_load_world=0`):
- Guest loads its own save. Record its game food total (Cell: the game's food total, +0x1C of the object cellsync reads with `S()`) and DNA (Creature: `GetEvolutionPoints`), and count its own NPCs from the game's object list.
- Join the host and wait 10 s.
- Assert on the guest: food, DNA and the NPC count are unchanged, and no creature of a host-only species exists in the guest's world.

It fails on main (food and DNA equal the host's, NPC count 0) and with `coop.fix same_place off`; it passes with the fix.

### Related
#13 (a received world overwrites the slot being played with `auto_load_world=0`), #65 (received world over old co-op slots), #60 (guest keeps creature pointers while the host is away), BUG-0020 (our objects go into saves), docs/known-issues.md "Creature: puppets still go into saves".

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #109.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: a guest in another world of the stage applies nothing of the host's world (coop_join_other_world).
- 2026-10-28: released in 0.7.3.0.
