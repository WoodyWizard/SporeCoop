---
id: BUG-0976
title: Cell: cell_levelup_view fails about half its runs on the level-ups: the guest loses 76-100% of its screen on an up change where the host's game loses 0-60%
status: open
kind: test
stage: Cell
reported: 2026-11-12
source: fix/w48-cellcull labs 20261112-195306-2, 20261112-200916, 20261112-202450
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
Seen while checking BUG-0975 on lanes 6 / 7 (GOG 2024). `cell_levelup_view` (BUG-0635) failed its screen check in 5 of
7 runs: guest 55-82 % lost on average against host 28-47 % (limit host + 20 %). The failing changes are the level-UPS
(+150): the guest lost 70-100 % of its screen, the host's game 0-60 % (often with fewer than 8 cells on the host's
screen). The downs were 0-53 %. Same with BUG-0975's switch off (`SPORE_FIX_OFF=cell.tile_cap`, lab 20261112-200916:
1 of 2 failed) and with the final BUG-0975 code, whose hold does not run in this test (0 skipped passes, lab
20261112-202450: 1 of 2 failed). Earlier history: 61 % / 81 % failures on 2026-11-07 (labs 20261107-161122,
20261106-224735). So not caused by BUG-0975; the test or the BUG-0635 fix (`cell.levelup_keep`) does not hold on ups.

How to reproduce: `tools/lab.py run cell_levelup_view --lane N --repeat 4`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
