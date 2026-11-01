---
id: BUG-0306
title: Cell, 4 players: the host faults every frame in IsDamageable (null chase target copied from a group leader by PickTargets)
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign#2
role: host
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w12-cell-fault
commits: aa2d02e, 111ca3e
tests: cell_stale, campaign
signature: dc04aee5:0x00E52949 (IsDamageable+0x9; named App::cCellModeStrategy::OnMouseMove+0x1939 by the crash tools)
docs: docs/re/cell.md#cell-pick-stale
related: BUG-0295, BUG-0174
---

## Report
Lab 20261030-104149 campaign#2 (main 813f8cd, lane 4, 4 players, the machine overloaded: host fps
17-21 at times). In Cell play the host logged `0xC0000005 at 0x00E52949 (data 0x108) esi=0` once in
its game log (11:47:35, 297 s into the session, level 4, ~1300 live cells) and then the same fault
at every frame: `exc` counted 1038, 1126, 1101, 1346, ... 8616 by the end of the Cell stage (~35/s),
the console stalled for 10 s at each check ("no frames for 15 s while loading a world" is the
monitor's reading of those stalls). It stopped when the stage was left. Seen once in the lab
history (no other log has 0x00E52949).

## Root cause
Observed (stack words of the fault + GOG 2024 disassembly, docs/re/cell.md#cell-pick-stale): the
fault is IsDamageable 0xE52940+9 with ESI = NULL, called by 0xE57CE0(cell 0x1C80C870, NULL) from the
chase step 0xE79C30 (behaviour 0x1006 0xE7A0A0 <- AI 0xE7A190 <- UpdateCells 0xE7C8C0). 0xE79C30
passes `GetIfNotDeleted(+0x22C)` unchecked. The liveness check 0xE70E30 runs just before it and
clears a dead chase target, but it calls PickTargets 0xE70CF0 only after that check, and
PickTargets makes a group member copy its leader's +0x22C / +0x230 (and the flee pair) without
checking them. The crash tools' names (OnMouseMove, RestAfterBite, DestroyCell, PopulateTile) are
nearest-symbol names and wrong; neither PopulateTile nor our tile code is on the stack.
Inferred: the leader's target died after the leader's last check (it is later in the pool), the
follower re-picked in that frame (+0x238 timer, 0.25 s) and got the dead index. Then the
repetition: Wine swallows the fault and aborts the frame, so the leader never runs its check and
the follower's 0xE70E30 resets +0x238 to 0 when it clears the copy, re-picks at once and faults
again. A vanilla bug; ours in that four players (meals, kills, tiles leaving the active area) kill
many more chase targets per second, and low frame rates make the 0.25 s re-pick land in the window
more often. Not ours: the partner-tiles / budget code (BUG-0174, BUG-0295) is not on the stack; it
can only add deaths (cells of tiles going inactive).

## Fix
`coop.fix cell.pick_guard` (cellsync.cpp h_PickTargets, a hook of PickTargets 0xE70CF0, both roles,
puppets left alone): after the game's PickTargets a flee / chase target that is no live cell is
dropped with its timer (what 0xE70E30 does one frame later). `coop.pool` counts `stale_targets=`;
`coop.pool staletarget [n]` (tests, host) makes group leaders chase a dead cell and rest 60 s.

## Verification
FAIL: `SPORE_FIX_OFF=cell.pick_guard` cell_stale, lab 20261030-121635 (lane 2): 11 AI cells (ai type
0x1006) follow a cluster leader holding a dead chase target; host faults 1 -> 648 in 10 s at
IsDamageable 0x00E52949, the same signature as the campaign. PASS: cell_stale lab 20261030-121739
(lane 2): 10 followers, faults 0 -> 0, `stale_targets=323` (the guard dropped the copies).
Regression: lab 20261030-120947 (cell_4p x2, campaign_short x2, lanes 4/7, c2fcc91) passed 4/4.
Note: the first version of the test (leaders of the fixture's groups) passed with the fix off
(lab 20261030-120640, 20261030-120917): every group member at the cell fixture has ai type 0 (no
behaviours), so nobody copied anything; the test now makes AI cells join such a group.

## History
- 2026-10-30: opened.
- 2026-10-30: fix/w12-cell-fault: root cause by disassembly, guard `cell.pick_guard`, test cell_stale (not run yet).
- 2026-11-01: released in 0.7.3.2.
