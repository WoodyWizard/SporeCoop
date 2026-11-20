---
id: BUG-1333
title: Cell: after the host changes its own cell, the level rebuild makes wild kin of the cell a following guest is about to leave (BUG-1257)
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: lab
role: 
versions: 0.7.16
fixed_in:
branch: fix/w59-cell
commits: 505778b1
tests: cell_own_level10, cell_own_level10_follow_off, cell_tests own_cell_follow
signature: cell_own_level10 "no NPC of the previous own cell 4DA56CA5 is left (own species now BAF1AD40)"
docs: docs/re/cell.md#cell-kin-follow
related: BUG-1257, BUG-0508, BUG-0637
---

## Report
0.7.16 final check out/lab/20261119-214823 (rel/0.7.16 f6c16890, lane 4): cell_own_level10 FAILED "no NPC of the
previous own cell 4DA56CA5 is left (own species now BAF1AD40 x9)": after the host's own-cell change at level 10 its
pool had `4DA56CA5 x5`, a cell no player played any more. The triage (out/TRIAGE-0.7.16.md, package D) read it as
the guest's own kin (BUG-1257) with the guest still on 4DA56CA5; the logs say otherwise (below).

## Root cause
Observed (both game logs): the guest has no cell of its own and follows the host's change (BUG-0508): host s 53.723
`own cell is now BAF1AD40 (level rebuilt)`; the guest's `the host's cell changed 4DA56CA5 -> BAF1AD40: this game's
world species follows it in place` and its new key back on the host (`player 1 cell = BAF1AD40`) at s 54.250. The
host's rebuild populated the level in those 0.5 s, and BUG-1257's kin filter gave the kin nearest the guest the key
the host still had for it, 4DA56CA5 (creation_here counts a premade cell that is not the host's current one as the
guest's own). Those kin stay (a wild cell does not follow a player): copies of the old cell around both players, the
very sight of BUG-0637's report. Inferred: the second change of the run (back to 4DA56CA5) left none because the
rebuild removed the guest's puppet first (`puppet of Bob removed by the game itself`), so no partner centre was near
the new cells; the defect needs the population to run while the guest's centre is there.

## Fix
`cell.kin_follow` (cellsync_kin.cpp, `cell_logic::OwnCellFollow` in cell_logic.h): the kin filter remembers this
game's own cell (S+0x10) at each population and the one before; for 15 s after it changed, a partner whose cell is
still the previous one counts as following (its kin keep the world's cell, counter `follow=` in `coop.pool kin`). A
guest that chose that cell itself gets its own kin again after the window. Off: the partner's key at once (0.7.16
before). Unit test cell_tests own_cell_follow. Test: cell_own_level10 (which now also allows the cells the partners
play now, BUG-1257's kin: the check predates partner kin); twin cell_own_level10_follow_off.

## Verification
Lab not run yet (orchestrator: no lab until "lab ok", the final check runs on other lanes); commands below.
* FAIL run (before the fix): out/lab/20261119-214823 cell_own_level10 (lane 4).
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_own_level10 --lane N --retry 0 --no-build`
* twin, FAIL expected: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_own_level10_follow_off --lane N --retry 0
  --no-build` (it fails only when the host's rebuild populates near the guest's puppet, as in the first change of
  20261119-214823; a twin that passes is rerun once before it counts).

## History
- 2026-11-20: opened.
