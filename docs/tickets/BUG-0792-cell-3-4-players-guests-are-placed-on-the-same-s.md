---
id: BUG-0792
title: Cell, 3-4 players: guests are placed on the same spot after a load or a knockout respawn
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: fork nvstlex/SporeCoopNVST nvst/rivals-foundation 64471660, 3af94028, a4a99f94
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0352
---

## Report
No player report (found in a fork). With 3-4 players in Cell, after a load / join every guest's cell is placed at
host + (2.5, 1) and after a knockout respawn at partner + (2, 1): two guests land on the same spot and overlap; a
respawning guest can anchor to another guest instead of the host, so positions drift.

## Root cause
Observed in our code (confidence high for the placement, impact inferred): `mods/coop/cellsync.cpp:571-572`
(downed_tick respawn: `nearest_partner(1e9f, &at, nullptr, true)` then `at + (2, 1)`), `cellsync.cpp:3585-3592`
(join_after_load: nearest partner, `g_join_to = at + (2.5, 1)`), and join_partner (`cellsync.cpp:6300`). The offset is the
same for every guest and the anchor is the nearest partner (any player). BUG-0352 fixed the two-player case only.

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
## Verification

## History
- 2026-11-10: opened.
