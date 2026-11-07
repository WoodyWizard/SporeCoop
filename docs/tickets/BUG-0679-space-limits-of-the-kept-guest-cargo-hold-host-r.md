---
id: BUG-0679
title: Space: limits of the kept guest cargo hold (host restart, changes while away, cargo slots, non-spice cargo)
status: open
kind: bug
stage: space
reported: 2026-11-07
source: fix/w46-space-items (BUG-0678), 2026-11-07
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
