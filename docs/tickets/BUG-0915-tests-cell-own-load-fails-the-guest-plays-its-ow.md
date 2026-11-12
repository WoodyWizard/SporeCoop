---
id: BUG-0915
title: tests: cell_own_load fails 'the guest plays its own cell after the load' on lane 9 (the guest has 0 own creations: the per-world own-creature rule)
status: fixed
kind: test
stage: Cell
reported: 2026-11-10
source: fix/w47-play-bots lab out/lab/20261110-232109 (lane 9)
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-rest
commits: 133f1c02
tests: cell_own_load
signature:
docs: docs/06-testing.md#testing-w48-rest
related: BUG-0342, BUG-0349, BUG-0345
---

## Report
Run as a regression check of fix/w47-play-bots (the branch changes `coop.pool prey`, which this test prints):
cell_own_load failed two checks, "the guest plays its own cell after the load" and "after the reload the guest's game
plays its own cell again (4DA56CA5)"; its other checks passed (the guest eats, bites, the host draws the guest with
the cell it plays). `coop.creations` on the guest: `(0 own)`, world player species 4DA56CA5, both players' cell
4DA56CA5. The newest earlier logs of this test on disk are of 2026-10-31 (fix/w25-reports, lanes 5 / 6, all OK); it is
in no suite that the lab runs by default.

## Root cause
Proven (2026-11-12, fix/w48-rest): a test outdated by BUG-0345 / BUG-0638 (2026-11-04 / -11-06), not a game or mod bug.
The guest's choice (choices.ini, BAF1AD40) holds only in a world recorded for it (worlds.ini); the cell fixture's
world 9652F8AD had no record on the test profile (0 own creations), so the guest played the world's cell 4DA56CA5 by
the rule. Reproduced on lane 5 (not lane-specific). Was inferred: the guest plays the world's cell because it has no own cell chosen on this lane (the
per-world own-creature rule of BUG-0345, 2026-11-04, came after the test's last runs: docs/06-testing.md says tests
that need the guest's own creature choose it first); the failing checks read `coop.creations` before any command the
branch changed. Not checked on rel/0.7.12 without the branch.

## Fix
tools/tests/coop_cell_own_load.py: after the start the guest's record of this world is set (`coop.creations worlds set
cell BAF1AD40`) and the host's world loads once more (`coop.worldfix queue 1 0`), so the own cell comes with a world
load as for a player who played it there; the record is cleared at the end. Test-only (no switch).

## Verification
FAIL (the test before the fix): out/lab/20261112-143119 (lane 5: "(0 own)", the world's cell 4DA56CA5, both own-cell
checks failed, as on lane 9).
PASS: out/lab/20261112-143433, PASS2_0915.

## History
- 2026-11-10: opened.
- 2026-11-12: the test sets the per-world record and loads the world again (fix/w48-rest).
