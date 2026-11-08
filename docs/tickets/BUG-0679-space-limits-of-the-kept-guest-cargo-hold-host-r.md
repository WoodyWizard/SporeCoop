---
id: BUG-0679
title: Space: limits of the kept guest cargo hold (host restart, changes while away, cargo slots, non-spice cargo)
status: fixed
kind: bug
stage: space
reported: 2026-11-07
source: fix/w46-space-items (BUG-0678), 2026-11-07
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k56-space
commits: ad4dfd39
tests: space_items_limits, space_items_rejoin
signature:
docs: docs/re/space.md#space-kept-hold-limits, docs/06-testing.md#testing-space-kept-hold-limits, docs/known-issues.md#issue-space-kept-hold-limits
related: BUG-0678, BUG-0195
---

## Report
Found while fixing BUG-0678 (space.keep_items, space_trade.cpp, docs/re/space.md#space-kept-hold). The host keeps
each guest's own cargo hold and gives it back when the guest loads the host's world again. Not covered:
* memory only on the host: a host that restarts its game (or loads another Space world) starts with nothing
  kept, so a guest that joins then gets the host's hold, as before 0.7.10 (the same limit as BUG-0195's colonies);
* what changes in the guest's hold while it is not connected (a trade refund paid at the session end, items
  picked up after leaving) never reaches the host and is not in the kept hold;
* cargo slots only grow: a guest with fewer slots than the host keeps the host's extra empty slots;
* a non-spice cargo item (types 0/1/5/6: plants, animals, artifacts ~) the host's copy lacks cannot be made
  again (no creation recipe known): logged "could not come back", counted in `coop.space trade kept` failed=.
Reproduce: coop_space_items_rejoin.py, with the host restarted between the leave and the join (first point).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261108-021346, the local record off): after the host's restart the rejoined guest had the host's hold
(the host's spice, none of its own, no artifact, the host's 13 slots): the kept hold lived in the host's memory only.
(code) Nothing recorded the guest's hold while it was not connected; apply_inv only added slots (AddCargoSlots); and
only spice had a maker (CreateTradingObject). (objdump) The game's own makers of the other cargo are the ones its
mission rewards use (0xC53500): animals 0xAC0C00, plants the plant cargo cache 0xB90560 + Clone, artifacts 0xC87A60;
a free cargo slot goes with RemoveItem 0xFF3FD0 (docs/re/space.md#space-kept-hold-limits).

## Fix
space_trade.cpp, three switches:
* `space.keep_items_local`: the guest keeps its own hold per host world (host name + planet file) on its disk
  (`<AppData>\Spore\SporeCoop\holds\<crc>.txt`), written when it changes in a world whose hold is its own, also while
  not connected; a world loaded from the host again gets that hold 2 s after it appears (the host's kept copy is not
  needed). Covers the host restart (or another world loaded on the host) and the changes while away.
* `space.keep_items_slots`: the free cargo slots beyond the own count go (RemoveItem).
* `space.keep_items_cargo`: plant, animal and artifact cargo is made by the game's factories and added (AddItem).
Tests start without a record (twoinst forget_holds, `coop.space trade forget`).

## Verification
coop_space_items_limits.py (space_items_limits, lane 10): FAIL with SPORE_FIX_OFF=space.keep_items_local, lab
20261108-021346 (the host's hold after the restart: spice A missing, spice B there, no artifact, 13 slots);
SPORE_FIX_OFF=space.keep_items_slots, lab 20261108-021600 (13 slots, the host's); SPORE_FIX_OFF=space.keep_items_cargo,
lab 20261108-021732 (no artifact). PASS lab 20261108-021152 and 20261108-021951 (spice x10 with the change while away,
the artifact, no host spice, 10 slots, no fault). space_items_rejoin (BUG-0678) still PASSES (lab 20261108-021646).
Not covered by a test: animal and plant cargo (no species key in the fixture; the same reward-code path as artifacts).

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-k56-space): fixed (space.keep_items_local / _slots / _cargo); test space_items_limits FAIL x3 / PASS x2.
