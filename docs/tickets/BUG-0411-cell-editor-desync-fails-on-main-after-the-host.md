---
id: BUG-0411
title: Cell: editor_desync fails on main: after the host's evolution the guest's mating call finds no mate (mate=00000000)
status: open
kind: test
stage: Cell
reported: 2026-11-01
source: lab regression (fix/w33-unlocks), lane 6
role: guest
versions: 
fixed_in:
branch:
commits:
tests: editor_desync (tools/tests/coop_editor_desync.py, part 4)
signature:
docs:
related: BUG-0004, BUG-0177
---

## Report
`editor_desync` fails at part 4 ("after the host's evolution the guest's own editor still opens") on
lane 6, 4 of 4 runs: on fix/w33-unlocks (20261101-142144, 20261101-150148 x2) and on main f0cf695 itself
(branch at main: 20261101-151114, copied into fix/w33-unlocks' out/lab). Every other part passes (the
guest's own cell editor before, the host's vote and editor, the guest gets and shows the host's new cell,
the Creature part after). The guest's mating call after the host's evolution: `mate_button=1
mate=00000000 alive=0 dist=-1.0`: the game shows the button, but no mate comes (before the host's
evolution the same call gave a mate 11 m away and the editor). flaky.json had editor_desync 7 runs,
0 failed before 2026-11-01.

## Root cause
Not analysed (outside fix/w33-unlocks' task). Inferred: something merged on 2026-11-01 (main between the
last passing run and f0cf695) changes the guest's Cell pool after the host's new cell (the mate spawned
for the guest is missing or removed: compare BUG-0004's "mate removed as a stray cell").

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
