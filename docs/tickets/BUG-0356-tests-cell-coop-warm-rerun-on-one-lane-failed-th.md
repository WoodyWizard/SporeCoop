---
id: BUG-0356
title: tests: cell_coop warm rerun on one lane failed the knock-out part (revives 2 hp 5, no respawn, 59 m from the partner)
status: open
kind: test
stage: Cell
reported: 2026-10-31
source: lab 20261031-202750 cell_coop#2 (fix/w26-cell-spare, lane 8, warm reset)
role: guest
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
`lab.py run cell_spare,cell_coop --repeat 3` (fix/w26-cell-spare cd75347): cell_coop#2 ran warm on
lane 8 right after cell_coop#1 and failed from the revive check on: "the partner right next to it
revives it (revives 2, hp 5)", then "still knocked out after 6 s", "respawn after 10 s (respawns 1,
hp 1)", "next to the partner (distance 59.3)", "small again (0.55 -> 0.55)". The guest had been
revived (hp 5, downed 0) before the check read it. 3/3 passed right after on lanes 3/6/8 (lab
20261031-203415); flaky.json had 1 failure in 38 runs before. Not analysed; the particle pool was
not full there (unrelated to BUG-0355 as far as seen).
How to reproduce: `lab.py run cell_coop --repeat 6` (warm reruns on one lane).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened.
