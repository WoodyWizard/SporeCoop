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
tests: tribe_chiefs_apart (coop_tribe_chiefs_apart.py, a scenario)
signature:
docs: docs/06-testing.md#testing-tribe-chiefs-apart
related: BUG-0067, BUG-0081, BUG-0152
switch: tribe.spread_chiefs
---

## Report
Player request relayed by the owner (2026-10-31): in Tribe the players' characters (each player's
chief: the mod makes the lowest-id adult of each player's share of the tribe its chief, 1.3x big,
tagged) appear at the same spot, standing on each other, and sometimes get stuck that way; wanted:
each at its own spot nearby, on first arrival and after a rejoin / stage change.

## Root cause
Not reproduced (fix/w21-tribe, lanes 8 and 9, 2 players):
* tribe fixture: the two chiefs 26 m apart on both machines;
* members spawned for the players (`coop.tribe spawn 4`): 3-31 m apart, not on one spot; the next
  chiefs after `coop.tribe drop_chief` 3.1-13.5 m apart;
* both chiefs put onto one spot (`coop.tribe stack`, switch off): the game's own avoidance parts them
  within 1-2 s (1.8 m, then 4-5.4 m; another run 1.05 -> 3.4 -> 4.8 m), on both machines, and both
  walk afterwards; both players ordering their chiefs to the same point: 4.4-4.6 m apart there;
* a live Creature -> Tribe (campaign, 2 players, `tribe setup: spawned 6`): chiefs 3.9 / 4.6 m apart
  (host / guest), their bounding radii 2-3.9 m (close, bodies may touch, but not within 1.5 m).
Not tried: 3-4 players (later players' chiefs are members the game or the test spawns), a
player's own report with positions or a screenshot.

## Fix
Defensive (not shown to be needed): `tribe.spread_chiefs` (tribe.cpp spread_chiefs, host): two chiefs
that both stand still less than 1.5 m apart for 1 s are parted: the later owner's chief goes to its
own spot on a ring 9 m round the tribe's centre (a quarter turn per owner, on the game's ground);
the guests follow through the snapshots. Counter `spread=` in `coop.tribe`. `coop.fix
tribe.spread_chiefs off`: chiefs stay. `coop.tribe` member lines now also print the bounding radius.

## Verification
Scenario test `coop_tribe_chiefs_apart.py` (stack the chiefs, then: no two within 1.5 m on each
machine after 6 s, and each player's chief walks when its player orders it):
* with the switch off it PASSED too (lab 20261031-104935-fix-w21-tribe: 9.4 m apart after 6 s, the
  game parted them), so it is not a FAIL/PASS pair;
* with the fix: lab 20261031-110157-fix-w21-tribe (2 runs): 11.8 / 13.9 m apart, `spread=2` / `1`,
  both chiefs walked 12 m on both machines.
no-fail-run: the reported condition was not reproduced (see the root cause).

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w21-tribe): not reproduced with 2 players; defensive parting of stacked chiefs behind tribe.spread_chiefs; scenario test.
