---
id: BUG-0943
title: Tests: tribe_bounce_world forms no tribe since tribe.form_pace (coop.tribe form held at food 15): its 'scenario happened' check fails
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: fix/w47-getfilecount lab 20261111-131344-2
role: 
versions: tests only
fixed_in: unreleased
branch: fix/w47-getfilecount
commits: 698fac83
tests: tribe_bounce_world, realign_tribe
signature:
docs:
switch: none (test tooling)
related: BUG-0571, BUG-0564, BUG-0942
---

## Report
tribe_bounce_world (the BUG-0564 class test) fails on rel/0.7.12: "the host's reloaded world lacks the tribe formed
after its save (the scenario happened)". Its `coop.tribe form` answers `tribes 2 -> 2 ... (pace: food 15, threshold
100 x 2 players: held)`: no tribe is formed, so the host's reload drops nothing and the test checks nothing. The test
is in no suite run since 0.7.7 (its last logs are of rel/0.7.7), so nobody saw it.

## Root cause
Observed: tribe.form_pace (BUG-0571, 0.7.7) holds the third tribe until the party's food is over the game's
threshold times the players; the command goes through the same hook (h_Form) and the test's fixture has 15 food.

## Fix
The tests (tribe_bounce_world, realign_tribe) give the host 300 food before `coop.tribe form` (the same in the new
tribe_bounce_goal). No game code.

## Verification
* FAIL: lab 20261111-131344-2 (tribes 2 -> 2, the scenario check fails).
* PASS: lab 20261111-131651 (villages formed, the reloaded world lacks them, the guest's realign removes them).
* realign_tribe (coop_realign_planet.py --fixture tribe) had the same setup: FAIL lab 20261111-133233 ("the host has
  two NPC huts (1)"), PASS 20261111-133902 with the food first.
* Not checked: coop_tribe_leader_tool.py, coop_tribe_attack_spot.py, coop_tribe_copy_marker.py and
  coop_tribe_guest_tribes.py call `coop.tribe form` too (the last two call it on purpose or on the guest).

## History
- 2026-11-11: opened.
- 2026-11-11 (fix/w47-getfilecount): found while reproducing BUG-0942, fixed in the test.
