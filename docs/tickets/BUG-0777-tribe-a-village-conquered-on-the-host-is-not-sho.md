---
id: BUG-0777
title: Tribe: a village conquered on the host is not shown conquered on the guest (no conquest state row, suspected)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-10
source: Discord 2026-11-09 bug-reports
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-captures
commits: 89310a4c, 5a073eb7, 7930f984, 3ec69627
tests: tribe_conquest_guest
signature:
docs: docs/re/tribe.md#tribe-conquest-sync, docs/re/tribe.md#tribe-conquer-caption
related: BUG-0775, BUG-0761, BUG-0564, BUG-0565, BUG-0645, BUG-0567
---

## Report
Discord bug-reports 2026-11-09 (as BUG-0775, the Tribe reading): a village conquered while one player attacks shows
conquered on one game only.

## Root cause
* Observed (lab 20261112-004447, realign.hut_fall off): the host's raid ended an NPC village (hut state 2, the village's
  conquered flag cTribe +0x556 set); on the guest the hut stood at 1 hp (3/1000) and the village was not conquered.
* Cause (code + Ghidra): realign wrote `max(1, host's)` health ("the guest's game runs no defeat") and its rows had no
  dead state; the game's conquest is the hut combatant's own TakeDamage (0xC9CA30 -> message 0x58BADDD ->
  cTribe_SetConquered -> the mission ends), which never ran on the guest. docs/re/tribe.md#tribe-conquest-sync.
  The other direction of the Discord report is BUG-0879.

Earlier reading (2026-11-10):
Suspected, confidence low (no logs):
* The guest gets the hut's health (`realign.buildings`, `mods/coop/realign.cpp:36-44`), the villages' relation
  (`wsync.tribe_rel`, `wsync.cpp:61`), the goal card and progress (`wsync.tribe_goal`, `wsync.cpp:64`) and the tribe
  level (`wsync.tribe_level`, `wsync.cpp:872`).
* No row carries the conquest itself: the mission's completion (0xC2F7F0: +0x7C = 5, `TribeStage_AddGoalProgress`
  0xCDB520 kind 1 conquered / 2 allied, TRG_TotemPole_Conquer; docs/re/tribe.md) runs only in the game that saw the
  hut fall. A hut set to 0 by the realign write is not a TakeDamage, so the guest's game never completes the
  conquest: the village keeps its colours, map icon and state there.
* The other direction (the guest's game showing a conquest the host has not) is not explained by the code: a guest's
  raid order goes to the host. Unknown whether the guest's own game reacts to the relation going to the threshold.

## Fix idea
A wsync row per village: conquered / allied (the mission state or the village's own flag, to find with RE), applied
on the guest through the game's own completion path outside the frame (or the village's owner/colour setter), and
the cinematic not replayed. RE first: which field marks a conquered village.

## How to test
`tribe_conquer_orders` extended: after the host's conquest cinematic, assert on the guest the village's conquered
state (the field found by RE, the map icon) equals the host's within 5 s. Main: the guest's village stays
unconquered: FAIL. Also run it with the guest's raiders only.

## Fix
realign.cpp: the rows carry Obj.fl bit 1 (a hut's dead state, a village's +0x556); a guest whose village or hut is
down on the host takes the game's own killing blow on its hut (tribe::fell_hut), or cTribe_SetConquered when no hut
stands; a dead hut gets no health write. Switch `coop.fix realign.hut_fall off` (0.7.12).

## Verification
`coop_tribe_conquest_guest.py` (suite `tribe_conquest_guest`, tribe): the host forms an NPC tribe, the guest's and the
host's members raid its hut (cut to 2 hp); after the host's fall the guest's village must have +0x556 and its hut down
(`coop.realign planet village|hut` fl bit 2). Key frames: the burning hut on both screens.
* FAIL with `SPORE_FIX_OFF=realign.hut_fall`: lab 20261112-004447.
* PASS: lab 20261112-004329, 20261112-005111 (lane 5; with tribe_conquer_orders and tribe_away).

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
