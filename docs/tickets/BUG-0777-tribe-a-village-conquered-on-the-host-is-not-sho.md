---
id: BUG-0777
title: Tribe: a village conquered on the host is not shown conquered on the guest (no conquest state row, suspected)
status: open
kind: desync
stage: Tribe
reported: 2026-11-10
source: Discord 2026-11-09 bug-reports
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-conquer-caption
related: BUG-0775, BUG-0761, BUG-0564, BUG-0565, BUG-0645, BUG-0567
---

## Report
Discord bug-reports 2026-11-09 (as BUG-0775, the Tribe reading): a village conquered while one player attacks shows
conquered on one game only.

## Root cause
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

## History
- 2026-11-10: opened.
