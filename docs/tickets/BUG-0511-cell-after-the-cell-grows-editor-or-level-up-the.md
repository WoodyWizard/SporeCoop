---
id: BUG-0511
title: Cell: after the cell grows (editor or level-up) the other cells float around very fast on the guest, and a visual glitch while growing
status: fixed
kind: desync
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.4, 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-misc
commits: 0c898b87
tests: cell_grow (tools/tests/coop_cell_grow.py)
signature:
docs: docs/re/cell.md#cell-world-scale, docs/06-testing.md#testing-cell-grow
related: BUG-0480, BUG-0041, BUG-0425
---

## Report
0.7.3.4 forum post: very big sync problems when the cell grows (a new cell stage level): cells float around very fast. A direct message (0.7.3.6): a visual bug when growing up (minor). No log line or screenshot names it; the 0.7.3.4 guest log shows the host's editor -> world reload pattern (BUG-0508) around the growth.
Target: 0.7.4 (Cell).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (2026-11-03, fix/w40-misc, lab): a level change rescales the game's world over ~3.5 s (WorldScaleStep 0xE73E50 -> 0xE6DB70: positions, velocities, sizes times old/new world scale G+0x514C, about the origin; 0.5 / 1.5 / 5.0 at levels 1-3). The guest's change starts a few frames after the host's (it follows the shared food total), and the guest applied the host's cell poses (MSG_CELL_STATE, in the host's scale) unchanged: the host's cells on the guest were 14-18 m off (p90) during each change and glided back fast, measured with coop_cell_grow.py (FAIL run). The mod did not convert anything (no world-scale handling in cellsync.cpp before).
Inferred, not verified: the "visual glitch while growing" is the same rescale seen on the partner's puppet (MSG_STATE carries no scale): BUG-0519. Earlier guess: after a size change the guest's mirrors (driven to the host's positions) and the game's own scale of the view disagree for some seconds, or the guest's view zooms while mirrors keep the old scale's speed; may be the same as BUG-0480 (stale mirrors) after the pool swap at 'host back'. To reproduce: cell_coop with a level-up on the host (and one on the guest), watch the guest's mirrors' speed right after it.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
mods/coop/cellsync.cpp: the host appends its world scale G+0x514C to every MSG_CELL_STATE chunk (after the cells; older guests stop before it, so no new message id and old guests still work); the guest keeps each mirror's pose in the host's scale and draws it times host scale / own scale every frame (the size at receive, a new mirror's spawn position too). Switch `coop.fix cell.world_scale off`. coop.pool shows `world_scale= target= scale_converted=`.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL: out/lab/20261103-181829 (SPORE_FIX_OFF=cell.world_scale: worst p90 17.9 m and 14.5 m over two level changes)
- PASS: out/lab/20261103-181943 (worst p90 0.13 m and 0.12 m)

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), report text only.
- 2026-11-03: cause found, fixed and tested on fix/w40-misc (cell.world_scale).
- 2026-11-03: released in 0.7.4.
