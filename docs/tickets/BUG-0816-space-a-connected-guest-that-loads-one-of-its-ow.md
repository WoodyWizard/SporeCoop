---
id: BUG-0816
title: Space: a connected guest that loads one of its own Space saves gets that save's cargo hold replaced by its co-op hold
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
related: BUG-0709
---

## Report
A guest loads its single-player Space save while connected: its tools, spice and cargo become the ones of the co-op galaxy, and the save is written that way. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_trade.cpp:1406 inv_tick skips only the join world (`e == g_inv_join_epoch`) and never checks that the world is the host's; a later world counts as fresh, the host answers K_INV_KEPT (:1478-1481) and the guest runs apply_inv (:1512-1513). The host also stores the own save's hold as the guest's kept copy (:1491). In Space only money is held for an own save (BUG-0709). Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Run inv_tick's restore and the K_INV_KEPT handler only when the guest's world is the host's save (host_world::space_other false), as BUG-0709 does for money.

## How to test
Fixture of coop_space_own_save.py: the guest loads its own Space save while connected; `coop.space trade inv` before and after: FAIL = the hold changes to the co-op hold and `coop.space trade kept` restored rises; PASS = the own save's hold stays.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
