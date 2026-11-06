---
id: BUG-0593
title: Civ: a city's owner, the civilizations' relations and money differ between the games (detector planet_owner / state:relation rows)
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: guest
versions: 0.7.7-dev
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: 49ba4925 94022d49 9edaac57 51228919 66d28db1
tests: wsync_civ (coop_wsync_civ.py)
signature:
docs: docs/re/civ.md#civ-wsync-owners
related:
switch: wsync.city_owner, wsync.civ_rel, wsync.civ_money
---

## Report
The detector rows added in 0.7.7 (D0) show it in campaign_space (42 samples, 33 % of the Civ comparisons) and
play_civ: "1 of 10 building_city_hall with another owner (at (-300 409 -67): 800021CA (player) on the host,
000021CB on the guest)", the same for industry / entertainment / house buildings; "1 of 4 civilizations differ in
rel (000021CB: -3.21 on the host, 3.00 on the guest)"; "1 of 6 civilizations differ in money (000021D3: 0.00 on the
host, 2000.00 on the guest)"; civ_coop: "money -800 on the host, 0 on the guest"; play_civ / campaign_space:
"village: 11 on the host's planet, 12 on the guest's" (with BUG-0539's hut / tool / food mat rows). Looks like a city
the player took (captured or bought) stays the NPC's on the guest, with the relations and money of that NPC civ.
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run campaign_space` (and play_civ).

## Root cause
Observed (lab 20261105-122815, the fix off; Ghidra GOG 2024):
* The buildings: `cCity::Capture` 0xBE9430 sets the city's flags +0x2E0..+0x2E6 before `ChangeOwner` 0xBE8A20, and
  ChangeOwner gives the buildings, turrets and +0x308 parts the new owner (SetPoliticalID, the owner's models) only
  when +0x2E2 is set (`City_SetPartsOwner` 0xBE4C80). A guest applies a capture from MSG_CIV_STATE by calling
  ChangeOwner itself with the flags 0: the city was the player's on both games, its three buildings 000021CB on the
  guest, 800021CA on the host (the detector's planet_owner:building_* rows).
* The relations and money: no part of the mod shared them in Civ. The host's capture costs the captured nation's
  relation with the player (and the game's other events change it); an NPC civilization's money (+0x98) moves with
  its own AI. A guest's game kept its own (lab: -1.12 against the host's -5.12; 0 against the host's 1500).
* civ_coop's `state:money` row (-800 / 0): the test's `coop.civ build` spends without the price check on the host,
  a guest's total is floored at 0 (res.floor). Inferred: no player's case (the game refuses an unpaid building);
  not changed.

## Fix
Three wsync rows in Civ (mods/coop/wsync.cpp, the W1 table): `city_owner` (every city's owner and place; a guest's
city whose buildings or turrets have another owner than the city gets them through the game's City_SetPartsOwner,
not while its city planner holds the city), `civ_rel` (every nation's CalculateRelationship to the player
civilization, applied with ApplyRelationship by the difference) and `civ_money` (+0x98, written). Switches
`wsync.city_owner`, `wsync.civ_rel`, `wsync.civ_money` (`coop.fix <name> off`: the guest keeps its own). The city's
owner itself stays with MSG_CIV_STATE (civ.cpp guest_cities). Symbol SporeCoop::City_SetPartsOwner.

## Verification
wsync_civ (the host captures an NPC city, changes another nation's relation by -4 and its money by +1500; the
guest's own game is read with coop.wstate): FAIL with `SPORE_FIX_OFF=wsync.city_owner,wsync.civ_rel,wsync.civ_money`
out/lab/20261105-122815 (buildings 000021CB x3 on the guest, relation -1.12 / -5.12, money 0 / 1500, 4 detector
rows), PASS out/lab/20261105-122658 (buildings 800021CA on both, -5.12 / -5.12, 1500 / 1500, no detector row).
Detector, campaign_space with SPORE_DETECTOR=20: before (rel/0.7.7 final 20261105-015350) planet_owner:building_*
and state:relation in 42 of 127 Civ comparisons, state:civ_money 2; after out/lab/20261105-132636: no planet_owner
and no relation row in 65 Civ comparisons, civ_money 1 sample (an NPC's AI income between the two dumps, not
repeated); play_civ out/lab/20261105-124754: no relation / owner row, civ_money 1 of 30. The remaining
planet:building_* rows (a building at another place, not its owner) are BUG-0602.

## History
- 2026-11-05: opened.
- 2026-11-05: fixed by the wsync rows city_owner, civ_rel, civ_money (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
