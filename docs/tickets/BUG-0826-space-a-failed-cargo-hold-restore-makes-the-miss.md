---
id: BUG-0826
title: Space: a failed cargo hold restore makes the missing items permanent (the record and the host's kept copy take the reduced hold)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
