---
id: BUG-0589
title: Cell: cell_new_game: the guest swam into the same meat 6 times and its game never ate it (points unchanged, the meat alive on both games)
status: open
kind: bug
stage: Cell
reported: 2026-11-04
source: lab fix/w43-tests 20261104-232622 cell_new_game (lane 3)
role: guest
versions: 
fixed_in:
branch:
commits:
tests: cell_new_game
signature:
docs:
related: BUG-0293
---

## Report
cell_new_game (--diet carn --early --minutes 3, lab 20261104-232622, lane 3, fix/w43-tests with BUG-0293's
probe wait): the guest's probe chose the same meat c2572485249 six times (`coop.pool swimto <key> tp`, then swimto
for 3 s); its own points stayed 66 and the meat stayed alive on both games, 0/6. Both diets read carnivore=1. The
host's probe ate 1 and found no meat on its screen within 20 s in 4 tries. Not analysed: whether the guest's cell
reached the meat at all (no position rows), or its game refuses to eat it (a mirror the host owns?).
Reproduce: `tools/lab.py run cell_new_game --lane N`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
