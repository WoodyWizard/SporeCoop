---
id: BUG-0364
title: cell: a knocked-out cell still eats food, so it respawns bigger (cell_coop 'small again' failed)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-01
source: lab 20261101-023131 cell_coop (main d6b81d9, lane 3)
role: guest
versions: 
fixed_in: 0.7.3.4
branch: fix/w31-cell-gate
commits: f84df60
tests: cell_ko_food
signature: "own cell ate" lines between "ko: own cell state 1" and state 2/3
docs: docs/re/cell.md#cell-ko-eat
related:
---

## Report
smoke in lab 20261101-023131: cell_coop failed only "small again (target size 0.55 -> 0.41)"; in the
14 earlier runs that pass the respawn target was 0.28. The guest's log: knocked out at s 46.46
(state 1, hp 1), then "own cell ate +12 plant (total 44)" at s 46.99 and "+12 plant (total 56)" at
s 47.23, both while still knocked out; the respawn at s 56.47 came with food 56 instead of 32, so a
bigger target size. Observed: a knocked-out cell eats what it touches. Inferred: the respawn size
follows the food total, so the eaten food made it bigger. Not analysed further; 1 failure in 41
cell_coop runs.
How to reproduce: `lab.py run cell_coop --repeat 6`, look for "ate" lines between ko state 1 and 3.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (cell_ko_food with the fix off): the knockout holds the cell at hp 1 on its spot, but the
game's eat event still runs for it: EatFood 0xE7DF40 (event 0x12) -> OnEat -> AvatarEat ->
AddFoodPoints, +12 shared food per plant on both games, the plant eaten. Its chunk bites (events
0x13 / 0x14) were not stopped either (inferred from the same code path, not seen in a run). The
respawn's small body grows by food (+0.01 per point), so it came back bigger.

## Fix
EatFood is hooked (GOG 2024): while our cell is knocked out, a call with our avatar as the eater
returns at once (the food stays), and our mouth's chunk bites are left out; counted in `coop.pool`
as `ko_eats_blocked=`. Switch `coop.fix cell.ko_no_eat`.

## Verification
cell_ko_food (guest and then host knocked out, 4 plants touched through the game's eat event; the
food totals of both games): FAIL with `SPORE_FIX_OFF=cell.ko_no_eat` in lab 20261101-093513 and
20261101-094736 (guest 0 -> 36 on both games, host 36 -> 84), PASS in 20261101-094930 (2 runs,
0 -> 0, ko_eats_blocked 3-4 / 2) and 20261101-100440 (3 of 3, gates on).

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w31-cell-gate (cell.ko_no_eat, test cell_ko_food).
- 2026-11-02: released in 0.7.3.4.
