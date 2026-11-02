---
id: BUG-0446
title: cell: a guest's own Cell parts do not give their creature twins at Cell -> Creature (the guest loads the host's world)
status: open
kind: desync
stage: Cell
reported: 2026-11-01
source: fix/w36-parts-transition (BUG-0441 analysis)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creations.md#creations-cell-twins
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
