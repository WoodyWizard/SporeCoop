---
id: BUG-0340
title: Tribe: the players' chiefs appear at the same spot, standing on each other, and sometimes get stuck
status: open
kind: bug
stage: Tribe
reported: 2026-10-31
source: player request relayed by the owner (2026-10-31)
role: host
versions: 0.7.3.1
fixed_in:
branch: fix/w21-tribe
commits:
tests: campaign_tribe (coop_campaign.py: chiefs check after the Tribe arrival and a Tribe rejoin), tribe_chiefs_apart (scenario)
signature:
docs: docs/re/tribe.md#tribe-chiefs-arrival, docs/06-testing.md#testing-tribe-chiefs-apart
related: BUG-0067, BUG-0081, BUG-0152
switch: tribe.spread_chiefs tribe.place_chiefs
---

## Report
Player request relayed by the owner (2026-10-31): in Tribe the players' characters (each player's
chief: the mod makes the lowest-id adult of each player's share of the tribe its chief, 1.3x big,
tagged) appear at the same spot, standing on each other, and sometimes get stuck that way; wanted:
each at its own spot nearby, on first arrival and after a rejoin / stage change.

## Root cause
Observed with 4 players (fix/w21-tribe, live Creature -> Tribe, `coop_campaign.py --from creature --to
tribe --players 4`, `coop.tribe` with bounding radii on all four machines):
* the chiefs of players 1 and 2 arrive 2.0 m apart, the sum of their bounding radii 5.0-5.5 m, on
  every machine, standing still for 6+ s; 50 s later still 4.5 m apart (sum 5.0);
* the game's idle behaviour gathers a tribe's idle members round its centre: a chief moved away
  (also with a walk order there) is back within ~10 m of the others after some seconds;
* two chiefs put onto one spot on purpose are parted by the game's avoidance within 1-2 s (to
  1.8-5 m), so it is the arrival's placement, not a lock of the game.
With 2 players nothing was close (fixture 26 m, live arrival 3.9 / 4.6 m).

## Fix
* `tribe.place_chiefs` (the owner's request): at the arrival in Tribe and after a rejoin (the host's
  split of the player tribe, then every 2 s for 60 s), a chief closer than the sum of the bounding
  radii + 1 m to another chief is put on its own ring spot (12 m from the tribe's centre, a quarter
  turn per player, on the game's ground) and gets a walk order there; once per arrival per chief.
  No moves on later chief changes.
* `tribe.spread_chiefs`: later, two chiefs standing in each other (< 1.5 m for 1 s) are parted the same way.
* The campaign now checks the chiefs' bodies on every machine after the Tribe arrival and after a
  Tribe rejoin (twice, 8 s apart; a finding `stack`).

## Verification
Campaign `--from creature --to tribe --players 4 --minutes 2`:
* FAIL with both switches off: seed 777, lane 8 (out/w21/c4_off.txt of the worktree): after the
  arrival `1-2 4.3 m (bodies 5.2-6.0 m)` on all four machines, after Dave's Tribe rejoin `0-3 5.1-5.5 m`
  (bodies 5.9-6.1 m): finding [stack]. (An earlier run, before the check existed: 2.0 m apart.)
* with the fix: seed 778, lane 8: two chiefs placed, "apart" on all four machines (PASS of the check);
  seed 777, lane 9: two chiefs placed (host log), yet 8 s later players 1-2 4.5-5.2 m apart (bodies
  5.1-5.6 m) again: the game's idle behaviour brought them back. Not a reliable PASS.
* after a Tribe rejoin with the fix: "apart" on all four machines in both runs (Bob seed 777, Carol
  seed 778); without it (seed 777): Dave's rejoin left 0-3 5.1-5.5 m (bodies 5.9-6.1 m).
  Campaign RESULT: seed 778 OK, seed 777 FAIL (the arrival check only).
Scenario test `coop_tribe_chiefs_apart.py`: passes with and without the switches (the game parts
stacked chiefs itself).

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w21-tribe): not reproduced with 2 players; defensive parting of stacked chiefs behind tribe.spread_chiefs; scenario test.
- 2026-10-31 (fix/w21-tribe): reproduced with 4 players at the live arrival; arrival placement added (tribe.place_chiefs); it holds in 1 of 2 runs, the game's idle behaviour gathers the members again. Open.
