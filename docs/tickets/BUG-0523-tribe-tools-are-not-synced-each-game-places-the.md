---
id: BUG-0523
title: Tribe: tools are not synced: each game places the tools its tribes buy at its own spots
status: open
kind: desync
stage: Tribe
reported: 2026-11-03
source: BUG-0488 analysis (fix/w41-tribe-world)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0488, BUG-0514
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
