---
id: BUG-0627
title: cell_coop: the partner cell 0.38 m from its state once (limit 0.3), not reproduced in 7 reruns
status: not-reproduced
kind: test
stage: Cell
reported: 2026-11-06
source: lab rel/0.7.9 20261106-105201 cell_coop
role: 
versions: 0.7.9 1e155b53
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
rel/0.7.9 1e155b53 integration run 20261106-105201, cell_coop: `FAIL the partner cell is exactly where its state
says (max 0.38)` (limit 0.3; `samples 30: puppet vs remote state max 0.38, facing max 5.0 deg, vs the real host cell
avg 0.21 max 0.36`). Every other check of the test passed.

## Root cause
Observed: no code on the Cell sync path changed between the last passing integration run (20261106-040221,
5fcb9a3e: max 0.06) and this one (1e155b53: civ, terrain_diag, worldobj, tribe, world, teamplay hits counters,
game::set_object_position; `git diff 5fcb9a3e 1e155b53 -- mods sporekit`). The guest's game ran at `fps avg 24` in
that minute (the integration run shared the PC with three other agents; 53 fps in 040221). Reruns on this tree:
10 of 10 passed (lab 20261106-122407 x3 at max 0.04-0.06 on lane 3; 20261106-125619 x4 at 0.05-0.11 and the cold
20261106-131524 x3 at 0.11-0.20 on lane 2 while lane 3 ran play_civ): the value grows with the PC's load.
History: 0.03-0.08 in 30 lab runs since 2026-10-30, one 0.20 (rel/0.7.3.6 20261102-223713). Inferred: one sample
taken while the guest drew a late frame (the puppet is driven once per frame, the state it is compared with
arrives at 15 Hz); not a desync (the real host cell was 0.21 m away on average, under the 2.0 m check).

## Fix
No change: not reproduced, no cause in the merged code. Reopen with the run dir when it is seen again.

## Verification
no-fail-run: not reproducible (1 failure in 20261106-105201; 7 passing reruns 20261106-122407,
20261106-125619, 20261106-131524).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int3): closed as not reproduced (evidence above).
