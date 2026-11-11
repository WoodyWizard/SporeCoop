---
id: BUG-0942
title: Tribe: a guest's game runs into a fault storm in the game's Resource::PFIndexModifiable::GetFileCount (0x008E8230, read 0x154) 14 s after the host loaded its Tribe world again (813 caught faults, a 2 s hitch)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-11
source: fix/w47-final12b lab 20261111-113158, campaign (lane 2), Bob
role: guest
versions: 0.7.7 - 0.7.11.2
fixed_in: unreleased
branch: fix/w47-getfilecount
commits: 698fac83
tests: tribe_bounce_goal (coop_tribe_bounce_goal.py)
signature: 0xC0000005 at 0x008E8230 Resource::PFIndexModifiable::GetFileCount (read 0x154), calls 00C90741 00C30069 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C, a guest, Tribe
docs: docs/re/tribe.md#tribe-gone-village-mission
related: BUG-0676, BUG-0564, BUG-0943
---

## Report
The full campaign after BUG-0933's fix (fix/w47-final12b lab 20261111-113158, lane 2, four players) failed on
`FINDING [crash] Bob: report report-20261111-121512-i4.txt` and `[fault] Bob: 813 new caught fault(s)`. Bob's game
(a guest, Tribe): one exception line at 12:14:58.797, `0xC0000005 at 0x008E8230 Resource::PFIndexModifiable::
GetFileCount (data 0x00000154)`, eax 0, calls `00C90741 00C30069 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C
00922514 006B1C99 007C64B3` (nearest names: cTribe_SetChieftain+0x861 < SpeciesGoal_Refresh+0x4B9 <
TribeMissionManager_Get+0x1B0 < PlanetPopulate_ForStage+0x8BE < PlanetPopulate_ForStage+0x130 <
PreGameModeTransition+0x17E9: far past those functions, the names are not the functions), then a hitch of 2015 ms
("Present has not reached SporeKit for 2 s") and the fault storm report at 12:15:12 (592 in 10 s, 813 in all). The
game went on: Bob played Civ and Space to the end of the campaign, no further fault.
For a player: a stutter of a few seconds in Tribe and the "SporeCoop caught a problem" report.

## Root cause
Not resource code and not a regression of 0.7.12: the guest's goal card was on a village its realign destroyed.

Observed (Ghidra, GOG 2024; docs/re/tribe.md#tribe-gone-village-mission):
* `0x008E8230` is the shared 4-byte getter `mov eax,[ecx+0x34]; ret` (the name is one of its owners). ecx 0x120,
  read 0x154: the combatant state of a **null cTribeHut**.
* The chain: 0xC90730 (`[tribe+0x368]+0x120`, state == 2 "the hut is dead", no null check) < 0xC30000 the goal
  card's text of a cTribeToTribeMission (mission +0x80 = the village; reads the hut only when the tribe has no
  members) < 0xCD3740 the Tribe Mission Manager's update, which refreshes the current mission (+0x74) while its dirty
  byte +0x70 is set and clears the byte after < the simulator's subsystem loop 0xBA2550.
* One fault is a storm: the game's handler unwinds the update before the dirty byte is cleared, so every frame
  repeats it until the current mission changes.
* The campaign run: all three guests removed the same three villages at 12:14:46 (realign.villages, BUG-0564:
  `cGameNounManager::DestroyInstance` of the cTribe, the game itself never destroys a tribe in Tribe). Only Bob's
  goal card was on one of them (the detector's `Tribe:state:goal` sample: "(-17 -433 -260) on the guest", a removed
  village; Carol's and Dave's cards were on (-43 -505 -99), a village the host kept): only Bob had the faults. The
  storm ended when Bob's card moved to (-43 -505 -99).
* Reproduced on two games (below), the same address, data and chain: 1302 faults in 20 s.
* Asked for the removed village's own pol (manager vt 0x28 with a pol: the map's mission gets its text at once) from
  outside the game's update, the same read is an unhandled exception: the game dies (lab 20261111-132006,
  20261111-132012).
* Since 0.7.7: tribe::remove_village, realign.cpp and the goal row are the code of v0.7.7 (`git diff v0.7.11
  cfaf00c7 -- mods/coop/realign.cpp mods/coop/wsync.cpp` is empty, remove_village unchanged). The png records of
  BUG-0921, the looks of BUG-0906 / 0907 and BUG-0933 are not on the path (no frame of ours in the chain, no
  resource object involved).
Inferred: what set the dirty byte 12 s after the realign in the campaign (a relation or mission event of the game;
not traced). The earlier campaigns passed the step because no guest's card was on a removed village when such an
event came.

## Fix
Switch `realign.village_goal` (`coop.fix realign.village_goal off` = before), mods/coop/wsync.cpp:
* `wsync::village_gone(tribe)`, called by tribe::remove_village before the DestroyInstance: when the manager's
  current mission is of that village, the manager's own RemoveMission (vt 0x30, 0xCD37F0) runs outside the frame:
  the HUD card goes and +0x74 is released. The goal row (wsync.tribe_goal) puts the host's goal there when the host
  has one.
* A hook of the card text 0xC30000 (`SporeCoop::TribeMission_RefreshCard`): a mission whose village has no hut and
  no members (the game's own two tests before the read) gets no text. This covers the missions that stay in the
  manager's vector and map (a refresh by pol, a later SetTargetTribe) and the frames before the removal runs.
New symbols (GOG 2024): TribeMission_RefreshCard, cTribe_PopulationCount, TribeMissionManager_RemoveMission,
TribeMissionManager_Refresh. Test command `coop.wsync goal dirty [pol]` (the manager's refresh request).

## Verification
`tribe_bounce_goal` (tools/tests/coop_tribe_bounce_goal.py): the host forms villages its save has not, its goal card
goes onto one, the guest's follows; the host's game over and reload; the guest's realign removes the village; then
`coop.wsync goal dirty` on the guest and, later, `coop.wsync goal dirty <the village's pol>`. Checked in the guest's
game: no new fault (`exc`), the current mission is none or on a village of the host's world, no card, frames go on.
* FAIL with `SPORE_FIX_OFF=realign.village_goal`: lab 20261111-131344 (1302 new faults in 20 s, `0xC0000005 x1956 at
  0x008E8230 ... data=0x00000154`, calls 00C90741 00C30069 ... 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C, the
  fault storm report; the card still on the removed village, `card=1`), 20261111-132012 (1364; the game died at the
  pol refresh).
* FAIL with `BOUNCE_GOAL_PLANT=nohook` (the card removed, the fix off after it): lab 20261111-132006 (the game does
  not answer after the pol refresh: the text hook is needed too).
* PASS: lab 20261111-131646, 20261111-131649 (card dropped 1, 0 faults), 20261111-132009, 20261111-132015 (with the
  pol refresh: "card text skipped 1", 0 faults), 20261111-132724, 20261111-133609 (the final test, with key frames).
* By eye (`frames/tribe_bounce_goal/` of lab 20261111-133609, the guest): 01 has the card "Conquer: Attack the
  Cyan Village" over the "Interact" card; 02 (after the realign and the goal event) has only "Interact", the
  village's mark is gone from the minimap, the game draws.
* Related: campaign_tribe x2 20261111-132715, 20261111-133603, roster_portraits 20261111-132718, tribe_portraits 20261111-132721,
  tribe_goal and wsync_transitions 20261111-133230, tribe_bounce_world 20261111-131651 / 20261111-133233,
  realign_tribe 20261111-133902. The whole campaign: CAMPAIGN_HERE
* `make test`.

## History
- 2026-11-11: opened.
- 2026-11-11 (fix/w47-getfilecount): analysed (Ghidra), reproduced (tribe_bounce_goal), fixed: realign.village_goal. Old path (0.7.7), not a 0.7.12 regression.
