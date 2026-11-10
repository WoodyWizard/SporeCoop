---
id: BUG-0797
title: Cell: a guest in another Cell world than the host's (or still in Cell while the host is in Creature) gets an emptying pool, no loot, no NPC interaction
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md
related: BUG-0130 BUG-0231 BUG-0205 BUG-0427
---

## Report
No player report. A guest that joined from its own Cell game (no auto load), waits for the transfer, or is still in Cell while the host already plays Creature (the BUG-0638 path) swims in a pool that only empties: no new food or cells, kills drop no meat or part tokens, predators pass through prey; its food progress stalls.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): cellsync.cpp:1719 h_Populate returns 0, cellsync.cpp:2544 h_SpawnLoot returns, cellsync.cpp:1540 h_Pair drops every pair without the avatar, all gated only by is_guest() && in_cell() && !g_solo. g_solo comes from cc.host_away = g_host_away (coop.cpp:6823), which counts only the host in the menu / an editor / a load, not host_absent_for() (also guest_elsewhere), which npc, tribe and civ use since BUG-0130 / BUG-0231. With the host in Creature host_out_of_stage gives 0 and the modes differ. Confidence: high on the path; how long players stay there is not measured.

## Fix
Not fixed yet.

## Fix idea
cc.host_away = [] { return host_absent_for(Absent::Npc); } (or a Cell value of Absent), so the guest plays its own pool solo while the host is elsewhere; switch cell.solo_elsewhere.

## How to test
Guest `coop.worldfix autoload 0`, loads its own Cell save, joins the host. Over 60 s `coop.pool see` / `coop.pool npcs` on the guest falls (main) and stays (fix); kill an NPC: `coop.pool loot` loot_calls rises only with the fix. Second variant: host `coop.pool food 1000` to Creature while the guest sits in its own Cell editor, then the guest returns.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
