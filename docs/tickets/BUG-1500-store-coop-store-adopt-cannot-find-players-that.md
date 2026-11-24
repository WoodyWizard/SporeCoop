---
id: BUG-1500
title: Store: `coop.store adopt` cannot find players that have only world records (Tribe and Civ owners) (NET-4)
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: 95e8a495
tests: tools/tests/coop_tribe_owners_session.py --adopt (tribe_owners_adopt, twin tribe_owners_adopt_off), tests/store_tests.cpp (test_owner_markers)
signature:
docs:
related: BUG-1153, BUG-1154
switch: store.owner_names
---

## Report
External static audit of 0.7.15.5, item NET-4 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (player_store.h by_name, store.cpp adopt): `by_name` scans the store's players only, and Tribe
and Civ keep their owners' keys only inside the world records (tribe.owners, civ.owners); `Store::text()` drops a player
with no record, so after a reload those worlds had no stored players: `coop.store adopt` printed "0 stored players named
..." and the join hint (on_player_join) never fired in Tribe / Civ. Second claim: `adopt` wrote the file without
`collect()`, so the claims renamed in memory reached the file only at the next game save.

## Fix
player_store.h `Store::mark_owners(marker, owners, now)`: every key a claim names gets a one-byte marker record
(`tribe.owner` / `civ.owner` v1) that keeps its name in the file; a key the claims no longer name loses it. store.cpp
`store::mark_owners` (the present players' session names) is called from tribe_host member_claims_save and civ_host
claims_save; `coop.store adopt` collects before it writes. Switch `store.owner_names` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: tribe_owners_adopt, tribe_owners_adopt_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_owners_adopt --lane N --retry 0 --no-build` (next session the guest comes as another game, `coop.store
  adopt Bob` finds the stored Bob, the guest owns its members again on both games).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_owners_adopt_off --lane N --retry 0 --no-build` ("0 stored players named 'Bob'").
* Unit: tests/store_tests.cpp test_owner_markers.
0.7.17 release lab: PASS: tribe_owners_adopt (lab 20261124-020506); FAIL (fix off): tribe_owners_adopt_off (lab 20261124-013101).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (95e8a495); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
