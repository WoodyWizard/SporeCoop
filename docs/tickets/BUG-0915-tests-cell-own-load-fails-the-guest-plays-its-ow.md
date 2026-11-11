---
id: BUG-0915
title: tests: cell_own_load fails 'the guest plays its own cell after the load' on lane 9 (the guest has 0 own creations: the per-world own-creature rule)
status: open
kind: test
stage: Cell
reported: 2026-11-10
source: fix/w47-play-bots lab out/lab/20261110-232109 (lane 9)
role: 
versions: 
fixed_in:
branch:
commits:
tests: cell_own_load
signature:
docs:
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
Not analysed. Inferred: the guest plays the world's cell because it has no own cell chosen on this lane (the
per-world own-creature rule of BUG-0345, 2026-11-04, came after the test's last runs: docs/06-testing.md says tests
that need the guest's own creature choose it first); the failing checks read `coop.creations` before any command the
branch changed. Not checked on rel/0.7.12 without the branch.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Reproduce: `tools/lab.py run cell_own_load --lane 9` (out/lab/20261110-232109 of fix/w47-play-bots).

## History
- 2026-11-10: opened.
