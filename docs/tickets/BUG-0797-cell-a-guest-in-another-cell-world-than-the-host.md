---
id: BUG-0797
title: Cell: a guest in another Cell world than the host's (or still in Cell while the host is in Creature) gets an emptying pool, no loot, no NPC interaction
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-own-world
commits: dda0afe5, 1843ae9a
tests: own_cell
signature:
docs: docs/re/cell.md#cell-own-world-pool, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0130 BUG-0231 BUG-0205 BUG-0427
---

## Report
No player report. A guest that joined from its own Cell game (no auto load), waits for the transfer, or is still in Cell while the host already plays Creature (the BUG-0638 path) swims in a pool that only empties: no new food or cells, kills drop no meat or part tokens, predators pass through prey; its food progress stalls.

## Root cause
Observed at runtime (confirms the code reading). The guest hooks h_Populate (returns 0), h_SpawnLoot and h_Pair are gated by "a guest in Cell and not solo", and solo came only from g_host_away (the host in the menu, an editor or a load). A guest that loads a Cell world of its own while connected had 0 own cells in its game's pool for 60 s (lab 20261112-030821: `coop.pool census` strays 0, `coop.pool npcs 80` 0, cell_away solo=0). The second path (still in Cell while the host plays Creature) is the same gate: host_out_of_stage gives 0 when the host plays another stage.

## Fix
cellsync solo_tick also starts the guest's own pool on own_world::elsewhere() (another Cell world of the host's stage, mods/coop/own_world.h) and own_world::host_left() (the host's state in another stage, Creature or Space, for 2 s; host_world::host_in_other_stage, unit-tested). Switch `coop.fix cell.solo_elsewhere off`: the old gate.

## Fix idea
cc.host_away = [] { return host_absent_for(Absent::Npc); } (or a Cell value of Absent), so the guest plays its own pool solo while the host is elsewhere; switch cell.solo_elsewhere.

## How to test
Guest `coop.worldfix autoload 0`, loads its own Cell save, joins the host. Over 60 s `coop.pool see` / `coop.pool npcs` on the guest falls (main) and stays (fix); kill an NPC: `coop.pool loot` loot_calls rises only with the fix. Second variant: host `coop.pool food 1000` to Creature while the guest sits in its own Cell editor, then the guest returns.

## Verification
Test `own_cell` step 4: the guest (connected) loads a copy of the fixture planet under another name; its own live cells in the game's pool (`coop.pool census` strays minus dying) over 60 s.
* FAIL: out/lab/20261112-030821 (`SPORE_FIX_OFF=cell.solo_elsewhere`, lane 5): [0 x 12], solo=0; also out/lab/20261112-025524 (lane 4, the first metric: own creatures within 80 m all 0).
* PASS: out/lab/20261112-030818, out/lab/20261112-031057 (lane 4): 0 for the first 15 s, then 146-173 cells; solo=1.
* The host-in-Creature path is covered by the unit test of host_in_other_stage only (no lab run of a guest left in Cell).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with cell.solo_elsewhere on the shared own_world check; FAIL 20261112-030821, PASS 20261112-030818 / 031057.
- 2026-11-12: released in 0.7.13.
