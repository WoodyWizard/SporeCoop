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
tests: cell_new_game
signature:
docs: docs/06-testing.md#testing-harness-g1
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
* Observed (the report's runs): the failing probes were "none on screen" tries (a try spent after 60 frames when no
  meat was on the guest's screen at once) or 2/6. Meat is kill loot, rare right after a level change.
* Inferred: a try needs food on screen to measure eating at all.

## Fix
Test only (fix/w43-tests): a probe try waits up to 20 s for food of its diet on the screen; a try with none still
counts as not eaten; the 3-of-6 rule is unchanged. The 2/6 tries (meat there, not eaten) are not addressed.

## Verification
After the change: out/lab/20261104-232622 FAIL (host 1/6: four tries with no meat on screen in 20 s; guest 0/6: the
same meat six times, never eaten: new ticket BUG-0589). The wait did not make the probe pass; left open.

## History
- 2026-10-30: opened.
- 2026-11-04 (fix/w43-tests): probe tries wait for food on screen (left open: the 2/6 case).
