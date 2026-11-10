---
id: BUG-0798
title: Cell: a downed cell is held at its pre-rescale position across a level change, so the partner cannot revive it
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md#cell-world-scale
related: BUG-0519 BUG-0662
---

## Report
No player report. A player knocked out while the partner eats past a level line is pinned far out in the pool, away from the partner and the food; no revive is possible and it always ends in the small respawn, the camera jumps there.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): cellsync.cpp:516 stores g_down_pos once at the knockout; downed_tick (cellsync.cpp:540) calls stop_at(avatar, g_down_pos) every frame. The world rescale 0xE6DB70 divides every position by new/old scale (docs/re/cell.md#cell-world-scale), g_down_pos is never converted, so the cell is held 3x (level 1->2) or 3.3x (2->3) farther from the centre; the respawn without a partner uses the same stale spot. Confidence: high on the mechanism, medium on frequency (needs a level line crossed in the 10 s).

## Fix
Not fixed yet.

## Fix idea
Keep g_down_pos in world-scale units (position x cCellGame+0x514C at the knockout) and convert it with the current scale each frame, as the mirrors do (cell.world_scale); switch cell.down_scale.

## How to test
Guest `coop.pool selfhit` (knocked out), host `coop.pool food <next level line>` within 2 s. Assert the guest's own position (`coop.pool own`) is within revive range of the host's partner position (`coop.pool scalepos`) after the rescale, and that the host held next to it revives it. Main: tens of metres apart, no revive.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
