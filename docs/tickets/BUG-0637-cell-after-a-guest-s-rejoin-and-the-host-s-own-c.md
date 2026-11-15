---
id: BUG-0637
title: Cell: after a guest's rejoin and the host's own-cell change at level 10 the host sees cells that do not belong to the level
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-03
role: host
versions: 0.7.8
fixed_in:
branch: fix/w46-cell-p0
commits:
tests: cell_own_level10 (coop_cell_own_level10.py)
signature:
docs: docs/re/cell.md#cell-own-species
related: BUG-0342, BUG-0349
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-03 (the host, mark at s 630.3): "there are creatures that
shouldn't be in this cell stage". The host's shot is covered by the co-op window.

## Root cause
Observed (the host's log): the guest left at s 594.1 and rejoined at s 595.4; the host saved and shared its world
(3935 KB, done s 616); the guest reappeared at s 621.3. 0.4 s before the mark the host clicked the own-cell arrows in the
co-op window (s 629.0, 629.9), each "own cell is now ... (level rebuilt)" at level 10 (`tiles: check after the frame
between (level 10): pool live 0, list entries 21, dead entries 21`), own cell 2912DEEB -> 2912DEEA -> 2912DEEB; the
host's size 0.48 -> 0.55, its pool small (189-216).
Inferred: the level rebuild after an own-cell change at level 10 (or the rejoin share before it) fills the level with
cells of another level or the players' creations. Not verified. Related: BUG-0342, BUG-0349 (own-cell rebuild, fixed).

## Fix
None: not reproduced as a co-op bug (see Verification). The likely sight is the game's own: the level is populated
with NPCs of the player's own species (cells that look like the player's own, 6-8 of them at level 10 in a single
player's game, 15-18 right after a rebuild), and after each own-cell change they are of the new cell. With two
changes 0.9 s apart (the report) the player saw copies of two of his cells swimming around, in a world where they had
not been before.

## Verification
* Lab `coop_cell_own_level10.py` (Cell fixture, the host's food raised to 924 = level 10 in 100-point steps, then
  `coop.creations choose cell BAF1AD40` (a premade cell: the test profiles have no own ones) and back, each a level
  rebuild; `coop.pool species` lists the foreground creatures by model and level): out/lab/20261106-221411 and
  20261106-221722. Every creature of the host's pool after each rebuild is of levels 9-11 (food level 10); no model
  the level did not have before except the own species (4DA56CA5 x6-8 before any change, BAF1AD40 x16-18 after the
  change, none of the previous one left); the guest's mirrors list the same. The first run's check "the players'
  cells are not NPCs" failed on exactly that vanilla population (the own species is an NPC kind of every level);
  the test now checks the level range, no new model and no NPC of the previous own cell.
* The guest's own cell in the report is "the world's own" (choices.ini); it follows the host's own-cell change
  (2912DEEB -> 2912DEEA at s 629.3) and came back 1.4 s after the host's second click (s 631.3, the guest's log):
  not a lasting desync.
* Not covered: a guest's rejoin with a world share right before the change (the report's s 594-621); the share
  saves the host's world but changes nothing in its pool, and the rebuild frees the whole pool anyway.
## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: not reproduced in fix/w46-cell-p0: own-cell rebuilds at level 10 keep the level's creatures (labs 20261106-221411, -221722); the own-species NPCs are the game's own.
