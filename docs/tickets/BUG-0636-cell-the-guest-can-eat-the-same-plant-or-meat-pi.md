---
id: BUG-0636
title: Cell: the guest can eat the same plant or meat piece twice: its mirror is made again before the host's state drops it
status: fixed
kind: desync
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-03 (the guest's mark)
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-cell-p0
commits: d7a0a2c5
tests: cell_eat_once (coop_cell_once.py --case eat, SPORE_NET delay=150)
signature:
docs: docs/re/cell.md#cell-once
related: BUG-0002, BUG-0088, BUG-0589, BUG-0420
switch: cell.eat_once
---

## Report
Discord report 2026-11-06 (0.7.8), the guest's mark at s 593.4 (seen in the host's chat in discord-2026-11-06/Sterben-03):
"in cell stage I can sometimes eat the same big plant's part twice".

## Root cause
Observed (both logs): the guest sends "gone" for the same host index 2-4 times, 30-100 ms apart; the host applies the
first and answers "not here, no points" to the rest: guest `[s 581.883] sent gone c110625761 ... meal=450` and
`[s 581.917] sent gone c110625761 ... meal=451`; host `meal=450: applied`, `meal=451: (-) not here, no points`; the same
for c110494700, c108856376, c35652806. Over the session 193 "sent gone", 38 objects sent 2-4 times (12 plant, 26 meat).
Observed (code): the destroy hook (cellsync.cpp) erases the mirror from `g_host_of` / `g_mirrors` when the guest eats
it; no tombstone of recently eaten host indices. Until the host's state drops the index (about one round trip) the
guest's mirror pass makes it again and the guest's game can eat it again with the game's own food each time ("own cell
ate +3", then "sent food +3 (no host cell with it)", which the host applies).
Inferred: double food for the guest per repeated meal. Related: BUG-0002, BUG-0088, BUG-0589 (fixed); class BUG-0420.

## Fix
`cell.eat_once` (cellsync.cpp, the shared ledger `cell_logic::Once` of class BUG-0420, docs/re/cell.md#cell-once):
the guest keeps a tombstone of every host index it ate (ACT_GONE) for 5 s and makes no mirror of it from a state
chunk meanwhile (`eaten_skipped`); the host's state drops the index within a round trip. `coop.fix cell.eat_once
off`: 0.7.9.

## Verification
* Reproduced (lab, `coop_cell_once.py --case eat` over SPORE_NET delay=150: the guest eats a plant / meat cell
  with the game's EatFood and its mouth stays on the spot, the eat repeated every 40 ms for 1.5 s): the state
  chunks already on the way made the eaten cell's mirror again and the guest's game ate it again, 1-3 times per
  meal, as in the players' logs ("sent gone" of one host index 2-4 times). The host counted the food once (it
  answers "not here" and the totals take the points back), so the visible bug is the meal eaten again.
* FAIL (`SPORE_FIX_OFF=cell.eat_once`): out/lab/20261106-215119, 4 of 4 meals eaten again (1-3 times), and its
  retry 4 of 4.
* PASS: out/lab/20261106-215503 (#1, #2): 0 eaten again, 3-4 state samples of each eaten cell left out.
* Area tests: out/lab/20261106-223316 (cell_meat, cell_gone, cell_sync, cell_levelups, cell_new_game OK); with
  SPORE_DETECTOR=20: out/lab/20261106-223318 (cell_kill_once, cell_eat_once OK, no detector row).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: fixed in fix/w46-cell-p0 (cell.eat_once): a tombstone of the cells the guest ate; FAIL 20261106-215119, PASS 20261106-215503.
- 2026-11-07: released in 0.7.10.
