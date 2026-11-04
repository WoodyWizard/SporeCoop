---
id: BUG-0514
title: Tribe: the guest's tribe members cannot pick up tools (weapons) from tool buildings the host built, so the guest's army stays weak
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in: 0.7.5
branch: fix/w41-tribe-actions
commits: e2ac6e3b
tests: tribe_tools (coop_tribe_tools.py)
signature:
docs: docs/re/tribe.md#tribe-tools-guest
related: BUG-0058, BUG-0483, BUG-0524
switch: wobj.tribe_tool tribe.tool_hold
---

## Report
Guest's mark (0.7.3.6, report 20261104-021224): 'cannot pick up the weapon'; a direct message: 'Tribe stage has the most bugs: the guest cannot really pick up tools from host-built buildings, so the guest's army is much weaker (main Tribe problem)'. Host screenshot at the mark: both tribes' members around the host's village with a tool building of the host's.
Target: 0.7.5 (Tribe).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (GOG 2024, lab lane 2, docs/re/tribe.md#tribe-tools-guest):
* a tool the host buys is made by its own game's palette only (CreateTool in the placement 0xD13C05): the guest's
  game had no rack there at all (`coop.tribe tools` on the guest: only the fire pit), so the guest could not
  click it; nothing in the mod sent tools;
* when the guest's member does get a GrabTool order on a rack that both games have, the order reaches the host
  (`tribe last order: from=1 ... action=3 target=2/0/18C8F0C -> <tool>`) and the host's member takes the weapon
  (+0x102C = 1), but the guest's mirror never runs the grab behaviour (its AI is skipped) and kept tool 0: the
  guest saw its members come back empty-handed and its heal orders never apply (InputAction 0x28 takes only
  members with tool 9).
Inferred: "the guest's army is weaker" is mostly what the guest's screen showed (no weapons in hand) plus the
racks it could not click; on the host the guest's members fight with what they hold there.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
* `wobj.tribe_tool`: the player tribe's tools are host-owned world objects (worldobj.cpp kind `tribe_tool`): a
  guest makes a copy of every host tool it has none of within 2 m (same type, at the host's place, upright, the
  purchased bit), at most 3 tries per tool. A guest's click on it is the host's GrabTool on the host's rack.
* `tribe.tool_hold`: a member's held tool is a replicated field (fields.h kind `tribe_member_tool`, field `tool`):
  the guest's mirror gets the host member's tool with the game's SetSpecializedTool (0xC27B00).
* Not done: a tool a guest buys is still made in its own game only: BUG-0524.

## Verification
`coop_tribe_tools.py` (suite `tribe_tools`): the host builds a rack (`coop.tribe build 1 9`), the guest's game must
list it, a guest's member is ordered to it (`coop.tribe grab`, the order a click gives), the member must hold the
weapon in the host's game and in the guest's.
* FAIL `SPORE_FIX_OFF=wobj.tribe_tool,tribe.tool_hold`: out/lab/20261103-234416 (no rack in the guest's game).
* FAIL `SPORE_FIX_OFF=tribe.tool_hold`: out/lab/20261103-234532 (the host's member holds tool 1, the guest's mirror 0).
* PASS: out/lab/20261103-234305, out/lab/20261103-234732 (rack made on the guest, tool 1 on both in 3-37 s).
Not verified: the real click (DoToolActionForAllSelected) on the guest's copy (the test calls DoActionForObjects,
the hooked step under it); a player's own palette purchase (the fixture's palette is locked).

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
- 2026-11-03 (fix/w41-tribe-actions): host tools on the guests, held tools replicated; fixed.
- 2026-11-04: released in 0.7.5.
