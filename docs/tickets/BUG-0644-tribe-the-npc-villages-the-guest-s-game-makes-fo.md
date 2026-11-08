---
id: BUG-0644
title: Tribe: the NPC villages the guest's game makes for the host's tribes have other colours than on the host
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-13, Sterben-07
role: both
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-tribe-civ
commits: 80846470
tests: tribe_colors
signature:
docs: docs/re/tribe.md#tribe-id-colour
related: BUG-0431
---

## Report
Discord report 2026-11-06 (0.7.8): the guest (discord-2026-11-06/M0ng0r-13, s 4206) "village color is desynced"; the host
(Sterben-07, s 4209) "other villages have different color for guest and host".

## Root cause
Observed (the guest's log): "the host's NPC tribe N ... is not in this game: made it (species 29158397, archetype X)" for
tribe 1 at 18:38:54, tribes 2-4 at 18:44:15-18, tribe 5 at 18:56:17.
Observed (code): tribe.cpp make_npc_tribes builds them with `SpawnNpcTribe(pos, archetype, 0, 0, 0, species)`: only the
archetype and the species are copied; no colour (ID colour) is sent or set.
Inferred: the guest's game picks its own colour for each tribe it makes. Class BUG-0431 (requests the mod builds
incompletely). The colour field's offset is not known yet (RE needed).

Observed 2026-11-07 (Ghidra + lab): a village's colour is cTribe +0x234 mIDColorID (identity colour block +0x230, cached RGB +0x254), the next colour id of the game's list for each new tribe (0x053DBCF1 player, F4, F5, ...). The guest made the host's villages itself (SpawnNpcTribe) and picked its own next colours.

## Fix
`wsync.tribe_color` (BUG-0644): a wsync row of every village's colour id, matched by place; the guest stores it and clears the cached RGB. Off: as 0.7.8.

## Verification
Test tribe_colors (coop_tribe_colors.py): the host forms a village, the guest's game gives it another colour id.
- FAIL (SPORE_FIX_OFF=wsync.tribe_color): out/lab/20261107-081057 (still different after 15 s).
- PASS: out/lab/20261107-080948.
- Not verified: the redraw on the screen (the check reads the game's colour id field).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: fixed in fix/w46-tribe-civ (wsync.tribe_color).
- 2026-11-07: released in 0.7.10.
