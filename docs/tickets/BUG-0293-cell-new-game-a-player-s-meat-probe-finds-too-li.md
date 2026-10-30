---
id: BUG-0293
title: cell_new_game: a player's meat probe finds too little meat on its screen now and then (with and without partner tiles)
status: open
kind: test
stage: Cell
reported: 2026-10-30
source: fix/w9-cell-food lab 20261030-023719, -030946
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`cell_new_game` (after 3 min of bot play, food 170-400, levels 3-4) asks each player to swim into 6
meat cells on its screen (`coop.pool see 80`); 3 of 6 must work. On lane 2 (2026-10-30) a probe
failed with "none on screen" or 2/6 in 4 of 10 runs: with the partner tiles of BUG-0174 on (lab
20261030-022912 #1 host 0/6, 20261030-023719 #2 guest 2/6, #3 guest 0/6) AND with them off
(`SPORE_FIX_OFF=cell.partner_tiles`, lab 20261030-030946 #3, guest 2/6). Meat (kill loot) is rare
around a player right after a level change (RemoveCellsOfOldScale destroys every cell outside the
host's visible box), which these levels cross often. Not a fault, no crash. Likely a test threshold
problem; whether meat is rarer with the partner tiles was not measured (3 of 7 runs with them, 1 of 3 without, is not
enough to tell).
Reproduce: `tools/lab.py run cell_new_game --lane N --repeat 5`.


## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.

