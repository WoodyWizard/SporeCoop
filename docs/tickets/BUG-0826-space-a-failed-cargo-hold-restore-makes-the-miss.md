---
id: BUG-0826
title: Space: a failed cargo hold restore makes the missing items permanent (the record and the host's kept copy take the reduced hold)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: d8dc662e
tests: space_items_missed
signature:
docs: docs/re/space.md#space-s1-0713
related: BUG-0678
---

## Report
After a rejoin some items of a guest's hold (plants or animals, own spice without a free slot) do not come back, and they are gone for good. Found by reading the code.

## Root cause
Observed in the code for the logic, confidence high; how often a restore fails is unknown. space_trade.cpp:1424-1431 and :1512-1519: rec_own(e) runs whatever `failed` is; rec_tick then overwrites the record on disk (holds\<crc>.txt) with the reduced hold and g_inv_last.clear() sends it to the host as the new kept copy. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
When failed > 0, merge the items that were not restored back into the record and the kept copy (or do not record until the restore is complete).

## How to test
Put a type 0/1 (plant / animal) item into the guest's holds record by hand, rejoin: FAIL = `coop.space trade kept` shows failed>0 and the item is missing from the file afterwards; PASS = it stays in the record.

## Fix
apply_inv lists the items it could not make; they are kept for the Space world of that restore (memory) and merged
into what the guest records (holds\<crc>.txt) and sends to the host as its hold (not into a fresh hold, which is the
host's copy), so the next restore (a rejoin, a reload) tries them again. Root cause confirmed at runtime: with the
switch off the record and the host's copy lost the item right after the restore. A restore that fails for good keeps
the item in the record for as long as the player plays that host world (it is not made from nothing). Switch
`coop.fix space.keep_items_missed off` (0.7.12). `coop.space trade kept`: a `missed:` line.

## Verification
`coop_space_items_missed.py` (suite `space_items_missed`): the guest leaves, a plant cargo item the game does not
know (type 0, key 0DEC0826) is added to its record, it joins again: the restore fails for that item (`failed=1`).
* FAIL with `SPORE_FIX_OFF=space.keep_items_missed`: lab 20261112-034735 (the item gone from the record, the host
  keeps 9 items = the guest's 9).
* PASS: lab 20261112-034256 (the item still in the record, the host keeps 10 = 9 + 1).
* Not run: a real failure (spice without a free slot, an animal from another galaxy); the path after apply_inv is the
  same.
* PASS again: lab 20261112-040253; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (items a restore could not make stay in the record and the kept copy; switch space.keep_items_missed).
