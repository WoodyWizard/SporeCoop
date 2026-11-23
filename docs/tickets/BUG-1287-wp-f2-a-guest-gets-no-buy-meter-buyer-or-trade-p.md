---
id: BUG-1287
title: WP-F2: a guest gets no buy meter, buyer or trade price map of the host's cities, removed routes stay (fork BUG-9081)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9081
role: guest
versions: 0.7.15.2
fixed_in: 0.7.16
branch: fix/w56-sync
commits: bc78d7e3
tests: civ_economy_state, civ_economy_state_off
signature:
docs: docs/re/civ.md#civ-influence-relay
related: BUG-0494, BUG-0877, BUG-1285
---

## Report
Found by the Lineages fork (PR #196), its BUG-9081 (row 13, partly): BUG-0494 adds the host's trade routes on a guest but
never removes one; the buy meter (+0x758, buyer +0x75C) and the city's price map (+0x570) were not sent, so the
guest's game never offered Buy City (CanBuyCity 0xBD8110 needs the buyer and the meter >= 100; GetBuyPrice 0xBE93A0 is
-1 without the buyer's price entry). BUG-0877's test drove the purchase by console, not the guest's UI.

## Root cause
Observed (code + Ghidra, docs/re/civ.md#civ-influence-relay).

## Fix
`coop.fix civ.economic_state` (civ_influence.cpp, civ.cpp guest_roads): chunk 0xF9 also carries each city's buy meter,
buyer and price map; the guest writes them (price map with the game's index 0xBE08E0). A route between two of the
host's cities that the host's list (MSG_CIV_BUILD kRoadList) does not have is removed on the guest with the game's
RemoveTradeRoute at both ends, as the game's own removal 0xBDFB30 does. Test commands: `coop.civ canbuy <city>`,
`coop.civ unroad <a> <b>` (host). Not done: the guest's Buy City button itself is not clicked by the test (its
condition, the game's own CanBuyCity on the guest, is checked).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Lab (first 0.7.16 night run, rel/0.7.16 c7699dca, run dirs in the rel/0.7.16 worktree's out/lab/):
  PASS civ_economy_state, out/lab/20261119-104136 (the guest's city 1 has the host's buy meter 100 / buyer 800021CA and
  price entry, its CanBuyCity says yes at 2000; the host's removed route (0, 1) is gone on the guest too).
  FAIL twin civ_economy_state_off (SPORE_FIX_OFF=civ.economic_state), out/lab/20261119-105127, for the right reason:
  the guest's city 1 has buy 0.0 / FFFFFFFF and no price entries, its CanBuyCity says no (price -1), the route stays.

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): the first lab run passed, its twin failed for the right reason (run dirs above).
- 2026-11-23: released in 0.7.16.
