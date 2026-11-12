---
id: BUG-0816
title: Space: a connected guest that loads one of its own Space saves gets that save's cargo hold replaced by its co-op hold
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-own-world
commits: dda0afe5, 412bc56e
tests: own_space
signature:
docs: docs/re/space.md#space-own-save-hold, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0709
---

## Report
A guest loads its single-player Space save while connected: its tools, spice and cargo become the ones of the co-op galaxy, and the save is written that way. Found by reading the code.

## Root cause
Observed at runtime (confirms the code reading): a connected guest that loaded its own Space save got the co-op hold the host kept: its spice A went 0 -> 7 within 25 s (lab 20261112-030031). inv_tick took every later Space world as one loaded from the host; the host answered the fresh hold with K_INV_KEPT.

## Fix idea
Run inv_tick's restore and the K_INV_KEPT handler only when the guest's world is the host's save (host_world::space_other false), as BUG-0709 does for money.

## How to test
Fixture of coop_space_own_save.py: the guest loads its own Space save while connected; `coop.space trade inv` before and after: FAIL = the hold changes to the co-op hold and `coop.space trade kept` restored rises; PASS = the own save's hold stays.

## Fix
While own_world::space_other() the guest sends no hold (K_INV) and applies no K_INV_KEPT (it still acknowledges it, the host stops resending); while its save is not known yet it waits. `coop.space trade kept` shows own_save_held. Switch `coop.fix space.hold_own_save off`.

## Verification
Test `own_space` steps 1-3: spice A x7 in the co-op hold, then the guest's own save (a copy of the share save, no spice A) for 25 s (`coop.space trade inv`, the game's cPlayerInventory).
* FAIL: out/lab/20261112-030031 (`SPORE_FIX_OFF=space.hold_own_save`, lane 5): spice A 7 (the save has 0).
* PASS: out/lab/20261112-025609, out/lab/20261112-031100 (lane 5): 0 for 25 s.

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with space.hold_own_save; FAIL 20261112-030031, PASS 20261112-025609 / 031100.
