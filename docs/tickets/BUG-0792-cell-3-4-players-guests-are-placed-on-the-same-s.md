---
id: BUG-0792
title: Cell, 3-4 players: guests are placed on the same spot after a load or a knockout respawn
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: fork nvstlex/SporeCoopNVST nvst/rivals-foundation 64471660, 3af94028, a4a99f94
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-cell
commits: 3e554da8
tests: cell_spawn_slots, cell_tests player_spawn_slots
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0352
---

## Report
No player report (found in a fork). With 3-4 players in Cell, after a load / join every guest's cell is placed at
host + (2.5, 1) and after a knockout respawn at partner + (2, 1): two guests land on the same spot and overlap; a
respawning guest can anchor to another guest instead of the host, so positions drift.

## Root cause
Observed (lab 20261112-065700, `cell.spawn_slots` off, three players): Bob and Carol knocked out side by side 60 m
from the host respawned 2.3 m apart (both at host + (2, 1)); after the host's save and share both guests' cells stayed
at the pool's start 0.01-0.02 m apart: each one's nearest partner was the other guest, within the 15 m that
join_after_load takes for "joined", so neither moved. Code: downed_tick (respawn at the nearest helper + (2, 1)),
join_after_load and join_partner (nearest partner + (2.5, 1)).

## Fix idea
From the fork (nvstlex/SporeCoopNVST nvst/rivals-foundation 64471660, 3af94028, a4a99f94, c4056b03, 03483893, dbeaf991); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
They added a pure helper `cell_logic::player_spawn_offset(player_id, radius)` (cell_logic.h): radius clamped to 3..20
(default 6, non-finite -> 6); id 0 -> (0,0), 1 -> (r,0), 2 -> (-r/2, r*sin60), 3 -> (-r/2, -r*sin60): a fixed ring slot per
session player id. In cellsync.cpp `spawn_anchor` is always the host (a guest uses `C.host_pos`; if the host is not known
it keeps its position rather than anchoring to another guest), and `spawn_for_me(anchor, 4.5)` adds the slot of
`my_id()`. Used for the knockout respawn, the after-load join and join_partner; they also force one placement when a guest
first joins an open Cell session and on re-entering Cell (UNSURE whether we need that: our join_after_load probably
already fires then). Unit test `tests/cell_tests.cpp player_spawn_slots` checks slots are distinct and at the radius.

## How to test
Three players in Cell (lab, 3 lanes): reload the world, then knock out both guests and wait for the respawn. On each
guest's game read both guest cells' positions. FAIL: the two guest cells are within 1 m of each other; PASS: more than
4 m apart, each at its own slot around the host.

## Fix
cell_logic.h `player_spawn_offset(id, radius)` (radius 4 m clamped 3..20; the host at the anchor, guests 1-3 at 0 /
120 / 240 degrees, later ids on wider rings) and cellsync.cpp `spawn_spot`: a guest goes to its slot around the
HOST's cell (a respawn falls back to the old rule when the host is not here or knocked out; a load waits for the
host's cell), and join_after_load also moves a guest that has another player's cell within 2 m. The idea came from
the fork (NOT FINAL there), re-implemented. Switch `coop.fix cell.spawn_slots off` (0.7.12). Not taken: the fork's
forced placement on joining an open session (join_after_load covers it).

## Verification
`coop_cell_spawn_slots.py` (suite `cell_spawn_slots`, 3 players in one lane, ~1.5 min) and the unit test
`tests/cell_tests.cpp player_spawn_slots`: both guests knocked out 60 m away respawn; then a save and share reloads
both. In each guest's own game its cell and the other guest's must be over 4 m apart, both within 15 m of the host.
* FAIL with `SPORE_FIX_OFF=cell.spawn_slots`: lab 20261112-065700 (2.3 m after the respawn, 0.01 m after the load).
* PASS: lab 20261112-064328.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-cell): fixed (cell.spawn_slots), FAIL and PASS runs in Verification.
