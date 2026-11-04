---
id: BUG-0446
title: cell: a guest's own Cell parts do not give their creature twins at Cell -> Creature (the guest loads the host's world)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-01
source: fix/w36-parts-transition (BUG-0441 analysis)
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w41-cell-left
commits: 69bdd173
tests: parts_cell_twin (tools/tests/coop_parts_shared.py --from-cell --guest-cell-part)
signature:
docs: docs/re/creations.md#creations-guest-other-world
related: BUG-0441, BUG-0401, BUG-0316
---

## Report
Found while analysing BUG-0441 (not seen by a player or a test). The live Cell -> Creature start (0xE83910)
unlocks the creature twin of every Cell part the cell had collected (table 0x1485FE8, 12 pairs;
docs/re/creations.md#creations-cell-twins). In co-op only the host's game makes that start; a guest loads
the host's world, so the twins are those of the host's Cell parts. Not checked: whether a guest's own
cell collects Cell parts in its game during co-op Cell (part tokens, BUG-0316), and if it does, whether
the party's rule (shared progress, the union) should give the guest's Cell parts' twins to everybody.

To check: `cell` fixture, the guest collects a part token the host has not (`coop.pool loot`,
`coop.pool swimto`), then Cell -> Creature (`coop_parts_shared.py --from-cell`); `coop.parts states`
on both games before the share, on the guest in Cell (are its cell keys of type 0x40616000 unlocked?).

## Root cause
Observed (lane 4): a guest's own Cell part collect (CollectPart in its game) did not reach the host's game before
0.7.4, so the host's live Cell -> Creature start (the only one in co-op) never unlocked its creature twin, and the
guest, loading the host's world, did not have it either (SPORE_FIX_OFF=parts.shared_cell: neither game has the
twin, lab 20261104-022323). Since 0.7.4 parts.shared_cell (BUG-0467) gives the host the guest's part within 2 s,
and the twin is unlocked on both games (lab 20261104-022114).

## Fix
No new code: covered by `parts.shared_cell` (BUG-0467, 0.7.4). New test parts_cell_twin.

## Verification
FAIL: SPORE_FIX_OFF=parts.shared_cell parts_cell_twin, lab 20261104-022323.
PASS: parts_cell_twin, lab 20261104-022114 (part 4, twin 32AC9620 IsUnlocked on host and guest).

## History
- 2026-11-01: opened.
- 2026-11-04: fix/w41-cell-left: checked: fixed by parts.shared_cell (0.7.4); test parts_cell_twin.
