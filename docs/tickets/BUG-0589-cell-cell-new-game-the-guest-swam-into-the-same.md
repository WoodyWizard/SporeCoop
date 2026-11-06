---
id: BUG-0589
title: Cell: cell_new_game: the guest swam into the same meat 6 times and its game never ate it (points unchanged, the meat alive on both games)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-04
source: lab fix/w43-tests 20261104-232622 cell_new_game (lane 3)
role: guest
versions: 
fixed_in: 0.7.8
branch: fix/w44-tests
commits: ef4a921e 5181bc99 9fb49030 250e2ef3
tests: cell_new_game
signature:
docs: docs/06-testing.md#testing-harness-g2
related: BUG-0293, BUG-0604
switch: none
---

## Report
cell_new_game (--diet carn --early --minutes 3, lab 20261104-232622, lane 3, fix/w43-tests with BUG-0293's
probe wait): the guest's probe chose the same meat c2572485249 six times (`coop.pool swimto <key> tp`, then swimto
for 3 s); its own points stayed 66 and the meat stayed alive on both games, 0/6. Both diets read carnivore=1. The
host's probe ate 1 and found no meat on its screen within 20 s in 4 tries. Not analysed: whether the guest's cell
reached the meat at all (no position rows), or its game refuses to eat it (a mirror the host owns?).
Reproduce: `tools/lab.py run cell_new_game --lane N`.

## Root cause
Decided: **the test bot** (its probe), for the cases seen; plus one real bug found on the way (BUG-0604, its own ticket).
* Observed (the probe now logs, for a try that gave no points, the cell in the eater's game and the avatar): in lab
  20261105-132739 #1 the guest's one miss of 12 tries was a meat it sat on, 0.33 m from its centre (avatar size 0.48,
  meat 0.14, foreground, the same level 3, alive on both): `swimto <key> tp` puts the cell beside the food without
  turning it and the swim goal then lies inside its own body, so it neither turns nor swims its mouth onto it. The
  other 11 tries ate (the game eats mirrored meat; points up, gone on both). Inferred: the original report's six
  swims into one meat (20261104-232622) were the same, the nearest meat right beside the cell each time (that run had
  no cell lines to confirm it).
* Observed: in 20261105-132739 #2 neither player found meat on its screen in any of 12 tries of 20 s: meat is kill
  loot and nothing had been killed near them (the bots had eaten the loot before). The probe measured the pool there.
* Observed (20261105-142923 #1, a real bug, BUG-0604): after the guest's level-up nothing was on its screen for 7 min
  (screenshot: blue water only), so its probe could find nothing. The test failing there is right.

## Fix
tools/tests/coop_cell_new_game.py (no mod change): near food (under 8 m) is swum to from where the cell is, mouth
first, for up to 6 s; the teleport only brings the cell near far food. With no meat on screen after half of the wait
the probe bites the nearest creature on its screen (the game's bite, `coop.pool kill <key>`) until it dies and waits
for its meat. Each try without points logs the cell's layer, level, size and the avatar's size, hp and place. The
3-of-6 rule is unchanged.

## Verification
* FAIL of the old probe: out/lab/20261104-232622 (fix/w43-tests worktree; the report), out/lab/20261105-132739 #2
  (no meat on screen in all 12 tries; #1 of that run passed with the teleport miss above).
* PASS: out/lab/20261105-142923 #2 (host 5/6, guest 6/6; the hunt made meat twice: `bit c...: gone`, then eaten) and
  #3 (host 3/6, guest 6/6). #1 of that run failed on BUG-0604 (the guest saw nothing after its level-up): a real fault,
  not the probe.

## History
- 2026-11-04: opened.
- 2026-11-05 (fix/w44-tests, G2): decided a probe problem (teleport beside the food, no meat to find); probe fixed; BUG-0604 split off.
- 2026-11-05: released in 0.7.8.
