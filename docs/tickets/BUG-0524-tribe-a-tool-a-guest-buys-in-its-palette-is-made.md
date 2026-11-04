---
id: BUG-0524
title: Tribe: a tool a guest buys in its palette is made in its own game only; the host has no rack there and the guest's members cannot take it
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: fix/w41-tribe-actions (found while fixing BUG-0514)
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-tribe-left
commits: 97b06246 748cf8ed
tests: tribe_tool_buy (coop_tribe_tool_buy.py)
signature:
docs: docs/re/tribe.md#tribe-tools-all
related: BUG-0514
switch: wobj.tribe_tool_buy
---

## Report
Found while fixing BUG-0514 (not run): the palette's placement (0xD13C05 / finish 0xD10F90) makes the tool in the
buyer's game. With BUG-0514's fix the host's tools reach the guests (worldobj kind tribe_tool), but a tool a guest
buys exists in the guest's game only: the host's game has no tool there, so a guest's GrabTool order on it is
rejected on the host ("rejected: no such target here") and the host's players never see it. The food is shared.
Wanted: a guest's purchase is a request to the host (the host makes the tool at that slot, the guest's own copy is
bound to it). Reproduce: needs a fixture whose tribe palette is not locked (the 'tribe' fixture's is).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (Ghidra, GOG 2024): the palette's placement start 0xD13C05 makes the tool in the buyer's game (CreateTool,
  the drag 0xD09FA0 sets spatial +0x72), the finish 0xD10F90 puts it into a village slot and sets the purchased bit;
  nothing reaches another game.
* Observed (runtime, lab 20261104-101357, the switch off): after the guest's purchase the host's game had no rack
  there (its tool list), so the guest's members could not use it.

## Fix
`wobj.tribe_tool_buy`: a guest's own player-tribe tool without a host partner (not dragged, not one it had at the
host's first snapshot, not a copy it made) is a request to the host (MSG_WOBJ kind 1 with id 0, the tool's state and
place, every 2 s for 20 s); the host's tribe.cpp buy_player_tool removes its tool of that type elsewhere (as the
palette's placement does) and makes the same at that place; the next snapshot binds the guest's copy, and the
guest's GrabTool order on it runs on the host's rack. The food was paid by the guest's palette (the shared food
ledger takes the change to the host). No new MSG id. `coop.fix wobj.tribe_tool_buy off`: the purchase stays in the
guest's game only (0.7.5).

## Verification
`coop_tribe_tool_buy.py` (suite `tribe_tool_buy`): the guest buys a rack (`coop.tribe buy`: the palette's calls,
dragged 4 s, then `buy_end`); the host must have no rack during the drag, then one within 2 m (its game's tool list);
a guest's member ordered to it must hold the weapon in the host's game and on the guest's screen.
* FAIL `SPORE_FIX_OFF=wobj.tribe_tool_buy`: out/lab/20261104-101357 (no rack in the host's game).
* PASS: out/lab/20261104-094957, out/lab/20261104-101636.
Not verified: the real palette UI (the fixture's palette is locked; the test calls the placement's functions);
a guest selling a tool (not sent: the host keeps it and the guest gets its copy back).

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w42-tribe-left): a guest's purchase is placed by the host; fixed.
- 2026-11-04: released in 0.7.6.
