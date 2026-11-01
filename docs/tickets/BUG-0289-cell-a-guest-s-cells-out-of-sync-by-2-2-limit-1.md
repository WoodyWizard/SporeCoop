---
id: BUG-0289
title: cell: a guest's cells out of sync by 2.2 (limit 1.2) in the 4-player campaign
status: fixed
kind: test
stage: Cell
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign#3 lane 7
role: guest
versions: 
fixed_in: 0.7.3.2
branch: fix/w9-cell-food
commits: e07d9e4, 6b23ebd, 1983bea
tests: campaign
signature:
docs: docs/06-testing.md, docs/known-issues.md
related: BUG-0263, BUG-0273, BUG-0282
---

## Report
Final 4-player campaign (main 3780da0, lab 20261029-203821 campaign#3, lane 7, run out/lane7/campaign/20261029-203834): "Bob: objects of kind c out of sync in cell (Bob:c:2.2)" (limit 1.2). Bob is the 2nd player, so this is not the extra-guest case of BUG-0275. No crash; the run reached the finale. Not analysed yet.

## Root cause
Observed (fix/w9-cell-food, 2026-10-30, from the run's logs and screenshots): Bob's game was **paused**.
The desync screenshot `campaign_01_desync_Bob.png` shows the pause menu ("Options", Resume Game)
open over the blurred pool; Bob's state lines show its cell at the same position (3.6 61.3) from
21:40:44 for over a minute (`input_lock=1` at the first of them: a caption was up when the campaign's
noise pause pressed Esc, and its "Resume Game" clicks were lost, as in BUG-0273). A paused game runs
no cell simulation: cellsync's guest_tick still steers the mirrors by velocity, which moves nothing,
and moves a mirror only when it is more than 3 m from the host's pose. Every mirror then trails
0-3 m (a sawtooth): p90 2.2-2.5 m, worst exactly 3.0 m in every sample of Bob, while Carol and Dave
(not paused, same fps ~65) were at 0.0-0.6 m. Not BUG-0282 (no low frame rate). Not a mod bug: the
player who has the pause menu open does not watch the pool; the check compared a paused game.

## Fix
The campaign's sync check (tools/tests/coop_campaign.py `check`) skips a guest whose pause menu is
open (`uifind.shown(con, 'Resume Game')`) and notes "sync: not compared (pause menu open)". No mod
change, so no switch.

## Verification
no-fail-run: a test-side fix of the 85-minute campaign; the cause is read from lab 20261029-203821
(campaign#3, lane 7: the pause menu on Bob's screenshot, its frozen position, the 3.0 m cap). Not
rerun on this branch.

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-cell-food: cause found (Bob paused, the pause menu left open); the campaign check skips paused guests.
- 2026-11-01: released in 0.7.3.2.
