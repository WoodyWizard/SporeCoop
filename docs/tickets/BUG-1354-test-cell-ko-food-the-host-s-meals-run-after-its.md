---
id: BUG-1354
title: Test cell_ko_food: the host's meals run after its knockout ended (8 s spent looking for plants)
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in: 0.7.16
branch: fix/w60-misc
commits: c74bf7ee
tests: cell_ko_food
signature:
docs:
related: BUG-0364, BUG-0746
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 4): cell_ko_food FAIL in the host's half: "plants to touch (1)", "still
knocked out after its meals (downed 0)", "no food for a knocked-out cell" on both games (0 -> 6). The guest's half
passed. History: 1 of 6 failed on rel/main, 8 of 24 on all branches since 2026-11-01.

## Root cause
Observed (cell_ko_food.host.game.log): the host was knocked out at s 50.77 ("ko: own cell state 1 ... left=10.0") and
respawned at s 60.77 (the 10 s knockout ran out), then "ate=1 (+6 pts)". The host's screen had one plant (status line
`plants=1`): the test's `plants()` looks for at least 2 plants with 8 tries of 1 s, and it ran AFTER the knockout,
so 8 of its 10 s were gone before the first meal; the second swallow (`kind 10 run`) ran on a living, respawned cell
and its meal counted, as it should. A test fault, not the mod: the first swallow, while still out, was blocked.

## Fix
coop_cell_ko_food.py: the plants are looked up before the hit (the eater does not move while it is out); the host
looks in four directions (8 tries) for 2 plants before its selfhit; the swallow's plant fallback uses those keys
instead of a new search; "still knocked out" is read right after the last meal, and the log prints how long the meals
took of the knockout. Nothing the test checks changed.

## Verification
no-fail-run: test-only change (the test's own timing; the mod's ko_no_eat / ko_no_swallow unchanged). Lab tonight:
cell_ko_food PASS; `SPORE_FIX_OFF=cell.ko_no_swallow` cell_ko_food FAIL (its twin). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_ko_food PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; the twin SPORE_FIX_OFF=cell.ko_no_swallow FAIL in 20261120-132626. Closed.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in the test on fix/w60-misc.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
