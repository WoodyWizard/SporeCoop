---
id: BUG-0773
title: Civ: a boosted vehicle that drops out of the host's vehicle table keeps its 1.5x speed for good (no restore)
status: open
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan), GitHub #199
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-boost-stacking
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

## History
- 2026-11-10: opened.
