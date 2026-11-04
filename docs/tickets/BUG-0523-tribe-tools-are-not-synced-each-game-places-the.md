---
id: BUG-0523
title: Tribe: tools are not synced: each game places the tools its tribes buy at its own spots
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-03
source: BUG-0488 analysis (fix/w41-tribe-world)
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-tribe-left
commits: 97b06246 748cf8ed
tests: tribe_tools_all (coop_tribe_tools_all.py)
signature:
docs: docs/re/tribe.md#tribe-tools-all
related: BUG-0488, BUG-0514
switch: wobj.tribe_tools_all
---

## Report
Split from BUG-0488 (the detector's `Tribe:unmatched:tribe_tool`, 4% of the Tribe comparisons of the 2026-11-03
night: "tribe_tool: 3 on the host, 3 on the guest, 1 host / 1 guest without a partner within 15 m", play_tribe#2,
#3 and campaign_tribe#2 of out/lab/20261103-012624).
* Observed (code): nothing in the mod sends or places tribe tools (cTribeTool, noun 0x18C8F0C): no CreateTool,
  no tool table in MSG_TRIBE_STATE, no worldobj kind. The tools of the world come with the save (the same on
  every game); a tool bought later is made by each game on its own (`cTribe::CreateTool` vt 0xB4 by the
  player's placement 0xD130D0 / 0xD10F90, or an NPC tribe's planner).
* Inferred: the same counts at other places are tools both games' NPC tribe planners bought (each picks its own
  free spot), or tools both players placed. Not checked at runtime which.
* Next: a host-owned table of each tribe's tools (type, position; MSG_WOBJ kind or the tribe state's tribe
  table), the guest's tool of that type in the mapped tribe placed at the host's position (tool spatial +0x34
  vt 0x38) or made / removed; to be done with BUG-0514 (taking the host's tools needs the same mapping).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (code, 0.7.5): the world-object kind `tribe_tool` (BUG-0514) covered the player tribe only; an NPC
  tribe's tools were each game's own (its planner, or the tribe tribe.npc_tribes made on a guest with the starting
  tools SpawnNpcTribe gives it at that game's spots), and a guest's own copy elsewhere was never removed.
* Observed (runtime, lab 20261104-101214, the switch off): the host's rack of a new NPC tribe was missing on the
  guest and the guest's own tool of that tribe (one the host has not) stayed.
* Observed (Ghidra): a tool the palette drags is already in the tribe's tool list (spatial +0x72 = 1), so the host
  sent tools at the cursor's places mid-drag; one tool per type and tribe (a placement of a type removes the old).

## Fix
`wobj.tribe_tools_all` (worldobj.cpp kind `tribe_tool`, tribe.cpp for_each_tool / make_tool / remove_tool): every
tribe's tools are host-owned world objects, state = type | host tribe index << 8 | player bit. A guest makes the
host's tool in its mapped tribe (only when that tribe has no tool of the type: one per type), removes its own tools
the host has not with the game's RemoveTool (kind drop_own), and the host skips tools being dragged.
`coop.fix wobj.tribe_tools_all off`: the player tribe's tools only, own copies stay (0.7.5).

## Verification
`coop_tribe_tools_all.py` (suite `tribe_tools_all`): the host forms an NPC tribe and places a rack for it, the guest
places an own tool of that tribe elsewhere; the guest's game (its tribes' own tool lists) must have the host's rack,
not its own tool, and every host tool near the village.
* FAIL `SPORE_FIX_OFF=wobj.tribe_tools_all`: out/lab/20261104-101214 (the rack missing, the guest's own tool stays).
* PASS: out/lab/20261104-095422, out/lab/20261104-101636.
Not verified: an NPC tribe's own planner buying a tool during play (the test places it with CreateTool as the
purchase does); tools made by the mod are not put into a village layout slot.

## History
- 2026-11-03: opened.
- 2026-11-03 (fix/w41-tribe-actions, BUG-0514): the PLAYER tribe's tools are host-owned world objects now (worldobj kind `tribe_tool`, `wobj.tribe_tool`): a guest makes a copy (same type, the host's position, upright) of every host tool it has none of within 2 m. Left for this ticket: NPC tribes' tools (needs the host tribe -> guest tribe mapping, tribe.cpp g_local_tribe_of); a guest's own copy at another spot is neither moved to the host's place nor removed (the kind has no apply / drop_own); tools a guest buys (BUG-0524).
- 2026-11-04 (fix/w42-tribe-left): every tribe's tools host-owned, own copies removed, dragged tools not sent; fixed.
- 2026-11-04: released in 0.7.6.
