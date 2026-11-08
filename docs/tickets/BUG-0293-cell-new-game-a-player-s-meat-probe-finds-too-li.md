---
id: BUG-0293
title: cell_new_game: a player's meat probe finds too little meat on its screen now and then (with and without partner tiles)
status: fixed
kind: test
stage: Cell
reported: 2026-10-30
source: fix/w9-cell-food lab 20261030-023719, -030946
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-g4-tests
commits: 4c327f51
tests: cell_new_game
signature:
docs: docs/re/cell.md#cell-eat-gate, docs/06-testing.md#testing-harness-g1
related: BUG-0589, BUG-0746
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

* G4 (2026-11-08, Ghidra static + runtime, docs/re/cell.md#cell-eat-gate): there is no size rule; what decides is the
  scale difference d = GetScaleDifference(eater, food) = clamp(food level - eater level + 2, 0, 4) (the player's level
  from its food points, 1 + food / 100). A carnivore mouth swallows meat only at d 0 / 1 (interaction kind 10, points);
  at d 2 (the same level) its bite breaks the meat and gives no points; plants are eaten at d 1. So the probe's
  "meat it swam into and did not eat" tries (the 2/6 case left open above, BUG-0589's 0.33 m miss "at the same level 3")
  were meat the game does not let that cell eat; and the eater's own lock +0x178 refuses every eat.

## Fix
Test only (fix/w43-tests): a probe try waits up to 20 s for food of its diet on the screen; a try with none still
counts as not eaten; the 3-of-6 rule is unchanged. The 2/6 tries (meat there, not eaten) are not addressed.

G4 (4c327f51): `coop.pool see` gets the scale difference as its last column; the probe swims only to food its cell
can eat by the game's rule (meat at d 0 / 1, plants at d 1), logs d for every try and the eater's own locks on a
miss. The 3-of-6 rule is unchanged.

## Verification
FAIL (the old probe): fix/w43-tests out/lab/20261104-232622, fix/w44-tests 20261105-132739 #2 / 20261105-142923 #1,
fix/w45-cell 20261106-003243 (host 0/6: the same meat 0.4-0.5 m from the cell four times, not eaten; guest 2/6).
PASS 3 of 3 on lane 2: out/lab/20261108-075019 (meat 6/6, 6/6, 6/6, 6/6, 6/6, 4/6; every eaten try at scale difference 1).

After the change: out/lab/20261104-232622 FAIL (host 1/6: four tries with no meat on screen in 20 s; guest 0/6: the
same meat six times, never eaten: new ticket BUG-0589). The wait did not make the probe pass; left open.

## History
- 2026-10-30: opened.
- 2026-11-04 (fix/w43-tests): probe tries wait for food on screen (left open: the 2/6 case).
- 2026-11-08 (fix/w46-g4-tests): G4: the cause is the game's eat rule by scale difference (docs/re/cell.md#cell-eat-gate); the probe chooses edible food; 3 of 3 PASS (out/lab/20261108-075019); fixed.
- 2026-11-08: released in 0.7.11.
