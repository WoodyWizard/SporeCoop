---
id: BUG-0356
title: tests: cell_coop warm rerun on one lane failed the knock-out part (revives 2 hp 5, no respawn, 59 m from the partner)
status: fixed
kind: test
stage: Cell
reported: 2026-10-31
source: lab 20261031-202750 cell_coop#2 (fix/w26-cell-spare, lane 8, warm reset)
role: guest
versions: 
fixed_in: 0.7.7
branch: fix/w43-tests
commits: 74fef091
tests: cell_coop
signature:
docs: docs/06-testing.md#testing-harness-g1
related: BUG-0396
---

## Report
`lab.py run cell_spare,cell_coop --repeat 3` (fix/w26-cell-spare cd75347): cell_coop#2 ran warm on
lane 8 right after cell_coop#1 and failed from the revive check on: "the partner right next to it
revives it (revives 2, hp 5)", then "still knocked out after 6 s", "respawn after 10 s (respawns 1,
hp 1)", "next to the partner (distance 59.3)", "small again (0.55 -> 0.55)". The guest had been
revived (hp 5, downed 0) before the check read it. 3/3 passed right after on lanes 3/6/8 (lab
20261031-203415); flaky.json had 1 failure in 38 runs before. Not analysed; the particle pool was
not full there (unrelated to BUG-0355 as far as seen).
How to reproduce: `lab.py run cell_coop --repeat 6` (warm reruns on one lane).

## Root cause
* Observed (logs): the warm reruns (20261031-202750 #2, fix/w31-civ-gate 20261101-103417 #2) started with the previous
  run's counters (`revives=1 respawns=1` in the first pool line) and the test of that day compared absolute
  counters: "revives 2", then "still knocked out" and "respawn ... (respawns 1, hp 1)" all follow from that. Fixed
  by BUG-0396 (ba7bb495, 2026-11-01 11:21): 40 of 40 cell_coop runs ok in the labs of 2026-11-03/04.
* Observed (fix/w31-civ-gate 20261101-100204, cold, before ba7bb495 but not a counter case): "revives 1, hp 6": the
  guest ate a plant right after the revive (own_eats 1 -> 2, own_points 20 -> 32); AvatarEat adds the food's
  hpValue up to 6 (docs/re/cell.md). The check allowed 3-5.

## Fix
Test only: the revive check allows hp 6 when the guest ate since the knockout (and prints the meals); otherwise
3-5 as before.

## Verification
no-fail-run: the meal case was not reproduced on purpose (a plant has to float into the revived cell); the old
failure is the run above. PASS with the new check: out/lab/20261104-225950 cell_coop#1, #2 (and the branch report's
regression run).

## History
- 2026-10-31: opened.
- 2026-11-01 (fix/w31-civ-gate): again on lane 9, warm rerun cell_coop#2 of lab 20261101-103417 (revives 2 hp 5, still knocked out, 50.1 m), and cold in 20261101-100204 (revives 1 hp 6); 2 of 4 runs that day. Also earlier in fix/w10-cell-4p 20261030-062715 and fix/w26-cell-spare 20261031-202750. The branch changes nothing in Cell.
- 2026-11-04 (fix/w43-tests): warm cases were BUG-0396's counters (fixed 2026-11-01); the cold case a meal after the revive; the check knows meals now.
- 2026-11-05: released in 0.7.7.
