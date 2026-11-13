---
id: BUG-1064
title: Cell: on a level-up the host's game removes about 15 points more of the cells round a partner than round its own cell
status: open
kind: bug
stage: Cell
reported: 2026-11-13
source: fix/w49-cell labs 20261113-021621, 20261113-022529-2 (BUG-0976 analysis)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0976, BUG-0635
---

## Report
Seen while closing BUG-0976 (cell_levelup_view, lane 4, GOG 2024). The guest 30 m from the host, the food +150 / -150.
On the UP changes the share of the guest's screen that is gone 5 s later was higher than the share of the host's own
screen in 10 of 11 up changes (guest / host: 91/77, 75/75, 95/83, 93/67, 94/43, 79/62, 89/88, 96/91, 89/79, 72/50,
85/62 %; labs 20261113-021621 runs 1-4 and 20261113-022529-2 has 95/87, 93/82, 14/78). Every one of those cells was
gone in the HOST's game too (`coop.pool alive` of the guest's screen keys on the host: 0 of 284 and 0 of 240+ gone only
on the guest), and the host's wipe destroyed none of them (`wipe_near_lost` 0): something else in the host's game
removes the old-level cells round a partner more than round its own cell. A game alone loses 43-91 % of its screen
on an up change anyway (the game's own), so the extra is about 15 points. Down changes, pooled per run: guest / host
21/21, 48/25 (one change 70 % against 20 %), 26/12 % (labs 20261113-021621 runs 1-2, 20261113-022529-2): also more
round the partner in most runs, all of it gone in the host's game too, none by the host's wipe.

How to reproduce: `tools/lab.py run cell_levelup_view --lane N --repeat 4`, compare the per-change lines.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. Inferred candidates: a level change flushes every partner tile list (h_PopTiles `all_go`) and the
partner passes are skipped while the world scale moves (~3 s), so the old cells round a partner are outside every
active tile for longer than the host's own; or the cull of the cell step uses only the host's own boxes.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
