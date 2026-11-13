---
id: BUG-1068
title: Tribe: a village only guests raided falls without counting for the stage's goal (the host's game makes the mission from its own input only)
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Inferred (docs/re/tribe.md#tribe-wsync): the village's cTribeToTribeMission (the goal) is made by
`TribeMission_SetTargetTribe`, called from the host game's input actions 0xC / 0xE / 0x10 (attack, gift on another
tribe's object); a guest's order runs on the host through host_command (DoAction) without that call, so the conquest
finds no mission and completes nothing. Not checked in Ghidra which call completes the mission (0xC2F7F0's caller).
Fix idea: the host's host_command calls SetTargetTribe (outside the frame, as wsync's goal_task) for a guest's
attack / gift order on another tribe's object.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
