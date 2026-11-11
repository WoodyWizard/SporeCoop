---
id: BUG-0942
title: Tribe: a guest's game runs into a fault storm in the game's Resource::PFIndexModifiable::GetFileCount (0x008E8230, read 0x154) 14 s after the host loaded its Tribe world again (813 caught faults, a 2 s hitch)
status: open
kind: crash
stage: Tribe
reported: 2026-11-11
source: fix/w47-final12b lab 20261111-113158, campaign (lane 2), Bob
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x008E8230 Resource::PFIndexModifiable::GetFileCount (read 0x154), calls 00C90741 00C30069 00CD3770 00BA266E 00BA1EE0 00B5D229 00CDF46C, a guest, Tribe
docs:
related: BUG-0676, BUG-0564
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
Not analysed. Observed around it:
* 12:14:44: the host loaded its Tribe world again (the campaign's reload step: `stage: Tribe -> Loading -> Tribe`,
  "tribe: the world changed (epoch 2)"); Bob stayed in its game and realigned: three villages "not in the host's
  world: removed here (BUG-0564)", tribe tools removed, at 12:14:45 and 12:14:46; the first fault 12 s later.
* The signature is in no other game log of rel/0.7.11, rel/0.7.12 and the fix/w47-* worktrees on this machine.
* The same step of the campaign passed in rel/0.7.12 lab 20261111-044257 and 20261111-090547.
Not from BUG-0933's change (inferred: that code is one integer compare per frame and a message the host did not
send here, its world id stayed 9652F8AD across the reload; the fault is in the game's resource code called from
its Tribe population code).

## Fix
None yet. Next step: name the callers (0xC90741, 0xC30069, 0xCD3770, 0xBA266E) in Ghidra: which resource index is
null (+0x154), and whether it is a tribe / village the realign removed while the game still populated it.

## Verification

## History
- 2026-11-11: opened.
