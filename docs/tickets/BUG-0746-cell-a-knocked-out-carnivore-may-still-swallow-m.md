---
id: BUG-0746
title: Cell: a knocked-out carnivore may still swallow meat (cell.ko_no_eat blocks EatFood and chunks, not the meat swallow of interaction kind 10)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-08
source: G4 static analysis for BUG-0293 (docs/re/cell.md#cell-eat-gate)
role: 
versions: 
fixed_in: 0.7.12
branch: fix/w46-b1-tribe
commits: 7773ebff, 9ff02322
tests: cell_ko_food
signature:
docs: docs/re/cell.md#cell-eat-gate, docs/re/cell.md#cell-ko-swallow
related: BUG-0364, BUG-0293
switch: cell.ko_no_swallow
---

## Report
Inferred from static analysis only (no run): meat (cellType 2, no AI) touched by a carnivore mouth at a scale difference
of 0/1 is swallowed through interaction kind 10 (0xE7E020 -> 0xE7D880 SwallowWhole -> 0xE7D800 -> OnEat -> AvatarEat:
points), not through EatFood (event 0x12, plants) or the chunk bites (0x13/0x14). The mod's `cell.ko_no_eat` (BUG-0364)
blocks EatFood and the chunks, so a knocked-out (downed) carnivore drifting onto edible meat may still eat it and get
points (cell_ko_food would be the test to extend). Reproduce: a downed carnivore cell next to meat one level smaller.

## Root cause
Observed (runtime, lab 20261108-142803, lane 7, `coop.fix cell.ko_no_swallow off`): our knocked-out cell (downed=1,
hp 1, both the guest and the host) ran interaction kind 10 on meat through the game's executor 0xE7E7F0 (`coop.pool
swallow <key>`): StartEatSwallow 0xE7E020 did not refuse (the downed avatar's `+0x178` lock is not set by the
knockout), SwallowWhole and FinishEatEvent -> OnEat -> AvatarEat ran and the shared food went 0 -> 12 on both games
(the guest), 12 -> 24 (the host). So the static finding holds: `cell.ko_no_eat` (BUG-0364) hooks EatFood (kind 0xB,
event 0x12) and the chunk bites, not the swallow of kinds 9 / 10.
Not seen in a player's log (no report); inferred: a carnivore (or a part type 3 mouth on a plant, kind 9) knocked out
next to food one level smaller eats it and respawns bigger, as BUG-0364 did with plants.

## Fix
mods/coop/cellsync.cpp: `Simulator::Cell::ExecuteInteraction` (0xE7E7F0, cdecl void(interaction*, float dt)) is hooked;
while our cell is knocked out (`g_downed`) an interaction of kind 9 or 10 with our avatar as the eater (+0) runs as
kind 1 (the plain contact push the executor uses when a swallow refuses), so nothing is swallowed and the food stays.
`coop.pool` counts it as `ko_swallows_blocked=`. Test command `coop.pool swallow <key> [kind]` runs the executor with
kind 10 (or 9) for the avatar and that cell, the call a mouth's contact makes. Switch `coop.fix cell.ko_no_swallow off`
(the code before: the swallow runs).

## Verification
Test cell_ko_food (coop_cell_ko_food.py, extended): the guest, then the host, knocked out; while out, besides the
EatFood calls of BUG-0364, `coop.pool swallow` on the two nearest meat cells (kind 10; no meat: kind 9 on plants); the
shared food on both games must not grow.
* FAIL (SPORE_FIX_OFF=cell.ko_no_swallow): out/lab/20261108-142803-fix-w46-b1-tribe (lane 7): "no food for a
  knocked-out cell" on both games, guest 0 -> 12, host 12 -> 24, swallows blocked 0.
* PASS: out/lab/20261108-142931-fix-w46-b1-tribe #1, #2 (lane 7): food 0 -> 0 on both games, swallows blocked 0 -> 2.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-tribe): reproduced (FAIL with the switch off), fixed with cell.ko_no_swallow, 2 PASS.
- 2026-11-11: released in 0.7.12.
