---
id: BUG-1068
title: Tribe: a village only guests raided falls without counting for the stage's goal (the host's game makes the mission from its own input only)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-raid
commits: 1d4fb2cd
tests: tribe_guest_raid_goal
signature:
docs: docs/re/tribe.md#tribe-guest-mission, docs/06-testing.md#testing-tribe-guest-raid-goal
related: BUG-0761, BUG-0777, BUG-0152
---

## Report
Found by fix/w49-tests (Q1, the campaign's end of Tribe), lane 4:
* exploration (tribe fixture, 2 players): the fixture's NPC village raided with `coop.tribe order <id> 15 <hut>` only
  (the DoAction a right-click gives, as the host runs a guest's order): its hut fell (state 2), the village got its
  conquered flag (`coop.realign planet village` fl=2), but the stage's goal bar stayed 0/5 and no conquest cinematic
  played. The same raid with the host's own right-click on the hut first: the bar went 0 -> 1 -> 2 at each fall.
* campaign_tc2 (4 players, lab 20261113-050826, out/lane4/campaign/20261113-050828): the raid's real clicks came from
  Bob and Carol (guests) only; the hut fell after 90 s, goal bar (0.0, 5.0) afterwards.
So in a session where the guests do the raiding (the host gathers, builds, or is the one who never attacks), a
conquered village does not count, and the Tribe stage cannot end by those conquests.
Reproduce: tribe fixture, a guest box-selects its members and right-clicks an NPC village's hut in attack mode
until it falls (`coop.tribe huthp <x y z> 40` to shorten); `coop.wstate` `progress=` stays 0 on the host.

## Root cause
Observed (runtime, lab 20261113-072615, `tribe.guest_mission` off): the fixture's NPC village (pol 0x21CB) raided by
the guest's member only (the guest's own order path: DoActionForObjects -> MSG_TRIBE_CMD -> the host's host_command,
DoAction 15 on the hut): the hut fell (state 2), the host's Tribe Mission Manager had no current mission before or
after (`tribe_goal` target FFFFFFFF), the goal bar stayed 0.0/5.0 for 30 s. A guest's gift (DoAction 9 on the
village) did not move the host's goal card either.
Known before (Ghidra, docs/re/tribe.md#tribe-wsync, #tribe-conquest-sync): the village's cTribeToTribeMission is made
by `TribeMission_SetTargetTribe` 0xCD3D70, which the game calls from its own input (DoActionGeneric 0xCD0C20, input
actions 0xC raid / 0xE, 0x10 gift) before the order; the mission's update 0xC2F750 ends it from the conquered flag
(+0x556) and adds the goal progress. host_command calls DoAction only, so a village only guests acted on had no
mission and its fall completed nothing.
Inferred, not traced: a village the host's game formed during play (`coop.tribe npc_tribe`) did get a mission at its
fall with the fix off (lab 20261113-072313: target set to it with state 5, bar 0 -> 1, 4 s after the fall); which
call makes it there (likely the tribe message handler 0xCD1D60 on the defeat message) is not known. The save's own
villages, the case of the report, get none.

## Fix
host_command (mods/coop/tribe.cpp): after a guest's order ran on at least one member, a raid (CitizenAction 15) or a
gift (9) on another tribe's object (the tribe, its hut +0x230, a tool +0x1F8, a member +0x1014; not the player tribe,
only a tribe of the world's list) calls `wsync::goal_target(village)`: SetTargetTribe(manager, village) outside the
frame (wsync's goal_task, which checks the tribe is still listed), unless it is already the current target. That is
what the host's own click does: the village's mission is made and the goal card follows the last village acted on
(the guests get it through wsync.tribe_goal). `coop.status` host line `guest_missions=` counts them.
Switch: `coop.fix tribe.guest_mission off` (SPORE_FIX_OFF=tribe.guest_mission): 0.7.13.

## Verification
Test `tribe_guest_raid_goal` (tools/tests/coop_tribe_guest_raid_goal.py, docs/06-testing.md#testing-tribe-guest-raid-goal):
the guest's member alone raids the fixture's NPC village hut (cut to 2 hp once it is there), the host's goal progress
must rise within 30 s of the fall; then the guest's gift on a village the host forms must move the host's goal card.
- FAIL with SPORE_FIX_OFF=tribe.guest_mission: lab 20261113-072615 (lane 6): bar 0.0 -> 0.0 after the fall, the gift
  did not move the card.
- PASS with the fix: lab 20261113-073140 (lane 7) and 20261113-073140-2 (lane 6): the fall counted 0 -> 1, the card
  went onto the gifted village. (20261113-072615-2 failed before the check: the lone raider stood at the hut without
  a blow until the village mended it; the test now sends it again after 15 s without a blow.)
- play_tribe --quick: lab 20261113-073337 (lane 7) OK.
- Earlier version of the test (raid on a village formed during play): 20261113-072313 (fix off) and -2 (fix on) both
  counted the fall, so that village does not reproduce the bug (see Root cause).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-raid (tribe.guest_mission), test tribe_guest_raid_goal.
