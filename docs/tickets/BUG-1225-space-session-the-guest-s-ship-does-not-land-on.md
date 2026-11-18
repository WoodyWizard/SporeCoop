---
id: BUG-1225
title: space_session: the guest's ship does not land on its restored colony in the lab (coop.targets finds no C/P planet)
status: fixed
kind: test
stage: Space
reported: 2026-11-17
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-space3
commits: 98350224
tests: space_session
signature:
docs: docs/06-testing.md#testing-space-session-rewards
related: BUG-1223, BUG-1220, BUG-1149
---

## Report
fix/w51-visual 20261117-120207 (space_session#1): in the next session the guest's ship did not land on its restored
colony 01460035 (`guest: could not pick planet 01460035 (C or P)`, the key frame shows the guest in its system view,
place 80460035); the host landed.

## Root cause
* observed: the colony exists in the guest's game after the restore: planet_line tech 5 with the guest's political id
  and its two cities with names, spice and buildings (every run), and in 20261117-124203 (lane 6) the guest landed on
  41460035 (frame space_session/01_colony_guest.jpg: its colony's buildings). Not a player-visible problem.
* inferred: in 20261117-120207 the "Explorer 2" Reward box (BUG-1223: a badge the test left pending in the first
  session, given after the restore) was over the guest's game during the landing tries and took the clicks and the
  zoom; `close_reward` closes it only once `coop.ui find` sees it, which right after the box appears it did not
  (seen on lane 6: four reads of `uifind.shown(.., 'Reward')` gave None while the box was on the screenshot). The
  frame was taken after the box was closed at the end.

## Fix
Test only: BUG-1223's change (the first session closes the boxes, none comes in the next session, checked);
`land_colony` prints the targets and the place when it cannot pick the colony (the next failure says which).

## Verification
* FAIL: fix/w51-visual 20261117-120207 (space_session#1, the guest not landed; another worktree's run).
* PASS: 20261117-124203 and 20261117-125525-2 (lane 6: the guest landed on 41460035 both times, frame
  space_session/01_colony_guest.jpg: its colony's buildings).

## History
- 2026-11-17: opened.
- 2026-11-17: fix/w51-space3: the colony is there; the landing failed under BUG-1223's box (inferred); test fixed with BUG-1223.
- 2026-11-18: released in 0.7.15.
