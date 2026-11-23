---
id: BUG-1467
title: tribe_held_item: the start checks fail while the host's chief holds its staff gesture (hold=1) for the whole sampling window
status: open
kind: test
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16.1
fixed_in:
branch:
commits:
tests: tribe_held_item (coop_tribe_looks.py --part item)
signature:
docs:
related: BUG-1449, BUG-0906
---

## Report
Lab 20261123-095115 (fix/w67-chief, lane 2 or 3), tribe_held_item#1: `FAIL start: the guest's game draws the copy of
the host's chief #1 with the staff in its resting grip (... still being taken 1 ...)` and `FAIL start: the copy keeps
the staff in the grip (... 0 samples without the host's hold, 20 with it)`. The key frame rows show the host's chief
itself in its long staff gesture 0x04C6CB02 with hold=1 (t 60.3, 62.0, 63.7 s) and the guest's copy the same (hold=1,
the same moment): both games agree. Runs 2 and 3 of the same run passed; the rejoin part of run 1 passed.

## Root cause
Inferred (test logic): the resting-grip check of `check_item` asks for the copy's hold=0 without excusing a hold the
host's member has too (BUG-1449 excused it only in the "keeps the grip" loop), and that loop needs at least 4 samples
without the host's hold in 20 s: a host chief that stays in its staff gesture longer than the window leaves nothing to
judge. Not a mod bug as far as these rows show.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened (fix/w67-chief, seen while verifying BUG-1450; not fixed there).
