---
id: BUG-0773
title: Civ: a boosted vehicle that drops out of the host's vehicle table keeps its 1.5x speed for good (no restore)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan), GitHub #199
role: host
versions: 
fixed_in: 0.7.13
branch: fix/w48-civ-speed
commits: 69eecd6d, 942fa887, 1342d697
tests: civ_boost_stack
signature: a vehicle keeps 1.5x +0x218 after its boost entry was dropped
docs: docs/re/civ.md#civ-boost-stacking, docs/re/civ.md#civ-boost-measured, docs/06-testing.md#testing-civ-speed
related: BUG-0771, BUG-0040, BUG-0223
---

## Report
Found while triaging GitHub #199 (BUG-0771): a second way the alarm boost can stay on a vehicle for good.

## Root cause
Inferred from the code, confidence medium:
* `mods/coop/civ.cpp:1909-1912` (boost_tick): an entry whose vehicle is not in `g_hv` (but still exists) is erased
  without putting `orig` back on the vehicle.
* A living vehicle leaves `g_hv` when its pol changes (`civ.cpp:2700-2708`, `g_fix_world` "other": a captured city's
  vehicles, a gift), and `on_world_change` calls `clear_coop_tables(false)` (`civ.cpp:4190`), which forgets every
  boost without a restore. That is right after a real load, but on_world_change also runs when the stage adapter
  saw no local entity (BUG-0223 class) while the vehicles still exist.
* The vehicle then keeps 1.5x (or more after BUG-0771) until the game's RecalcStats rewrites +0x218; the next alarm
  reads that value as `orig`.

## Fix idea
In boost_tick, when `exists(v)` but the vehicle is not in `g_hv`, write `orig` back before erasing. In
clear_coop_tables(false), restore for vehicles that are still in the game's vehicle list (`exists`), skip only the
freed ones. Same switch as BUG-0771 or `civ.boost_restore`.

## How to test
In `coop_civ_boost_stack.py`: boost a vehicle (`coop.civ alarm 0`), then take it out of g_hv while it lives (give it
to another nation or a test command `coop.civ hv drop <id>`), and run `coop.civ world_change` (the test path of
on_world_change). Assert the vehicle's +0x218 is back to its base within 1 s. Main keeps 1.5x: FAIL.

## Fix
civ.cpp: boost_tick (an entry whose vehicle left `g_hv`) and clear_coop_tables (now also called with restoring
from on_world_change) write the own speed back when `boost_ours`: the vehicle is in the game's vehicle list now, of
the nation it had when boosted, and +0x218 is still exactly `orig x 1.5` (the value the mod wrote), so after a real
load nothing is written into a new vehicle at a reused address. Switch `coop.fix civ.boost_restore off` = 0.7.12.
Found while testing: in normal play a living vehicle is never missing from `g_hv` at boost_tick time (it is pruned and
re-listed in the same 0.1 s step after boost_tick); the leak needs the tables cleared while the vehicles live (a false
world change, BUG-0223 class).

## Verification
Test `civ_boost_stack` step 3: an alarm, then (a) `coop.civ hvdrop <id>` (out of the host's table), (b)
`coop.civ world_change` (on_world_change while the world stays); +0x218 must be the own speed 1 s later.
* FAIL: out/lab/20261112-004150 (`SPORE_FIX_OFF=civ.boost_restore`, lane 3, only step 3 fails: 28.68 kept, own 19.12),
  also out/lab/20261112-003336.
* PASS: out/lab/20261112-002848, out/lab/20261112-003341, out/lab/20261112-003838 (19.12, `restored=1` / `=2`).
Proven with the forced paths; a real false world change with boosted vehicles was not reproduced.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-civ-speed (civ.boost_restore), FAIL 20261112-004150, PASS 20261112-002848 / -003341 / -003838.
- 2026-11-12: released in 0.7.13.
