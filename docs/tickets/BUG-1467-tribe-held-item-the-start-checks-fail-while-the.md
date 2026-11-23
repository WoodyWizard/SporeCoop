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
branch: fix/w84-tests
commits: 77531ae2
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
coop_tribe_looks.py check_item() (tribe_held_item, part item):
* the resting-grip wait skips every sample in which the host's chief itself has hold=1 (its staff gesture: the copy
  mirrors that hold, tribe.mirror_hold, BUG-1449); the wait goes on up to 60 s (was 30 s) while no sample could be
  judged. A host chief in its gesture for the whole wait is printed `(start: the host's chief #N held its staff gesture
  (hold=1) in every sample ...; BUG-1467)` and the grip check is not made.
* the "keeps the grip" loop waits up to 40 s (was 20 s) while it has fewer than 4 samples without the host's hold;
  fewer than 4 then is "not judged" (was a FAIL); 4 stuck samples while the host does not hold still FAIL.
* main(): when neither part (start, rejoin) judged the resting grip or the lasting grip, and no check failed, the run
  is `RESULT: INCONCLUSIVE ... (BUG-1467)` (a check that failed still makes it FAIL).
Test only, no mod change.

## Verification
Open, pending night run: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_held_item --repeat 3 --lane N --retry 0 --no-build` PASS (or INCONCLUSIVE naming BUG-1467, never the start
FAIL of a host-held staff); the twins still FAIL: `SPORE_LAB_FIXOFF="tribe_held_item=tribe.held_item" SPORE_DETECTOR=20 python3 tools/lab.py run tribe_held_item
--lane M --retry 0 --no-build` and `SPORE_LAB_FIXOFF="tribe_held_item=tribe.item_scale" ...`. FAIL before: lab
20261123-095115 tribe_held_item#1 (fix/w67-chief).

## History
- 2026-11-23: opened (fix/w67-chief, seen while verifying BUG-1450; not fixed there).
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): host-held samples skipped, nothing judged is INCONCLUSIVE (77531ae2). The ticket file came from fix/w67-chief (e47f50a5, rel/0.7.16.1) and was added to rel/0.7.17 by this branch as is.
