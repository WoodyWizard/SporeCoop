---
id: BUG-1499
title: Store: after_save opens the world again before the features collect, so they skip their records (NET-3)
status: open
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: 95e8a495
tests: tools/tests/coop_tribe_owners_reload.py --fresh-store (tribe_owners_reload_fresh, twin tribe_owners_reload_fresh_off)
signature:
docs:
related: BUG-1149
switch: store.save_opened
---

## Report
External static audit of 0.7.15.5, item NET-3 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (store.cpp after_save): when the save itself opened the world's store (`if (!g_open ||
g_cur.world != w) open_world(w);`, serial + 1) the records were collected at once, and every feature holding world data
refused its save because the store's serial was new to it (tribe_host member_claims_save, civ_host claims_save, the Space
kept_disk backends, space_rivalry disk_save; they take the serial in their own next tick). So the first save after a world
got its name, or a save within a second of a load (before store::tick), wrote no tribe.owners / civ.owners / Space records.
Memory survives, so the loss shows only when that save is the last one (Save and Quit).

## Fix
store.h `Feature::opened` (optional): store.cpp `opened_by_save()` calls it for every feature right after a save opened
the store, before `collect()`. Tribe and Civ run their claims read, match and follow at once (the owners of this moment);
the Space badges and colonies sync their kept stores from the disk (restore never replaces a live entry); Rivals takes the
store as this world's (its live Rivals state kept). The `coop.store save` test path does the same. Status `opened_by_save=`;
test seam `coop.store hold` (the store closed until the next save, as before a world has its name). Switch
`store.save_opened` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: tribe_owners_reload_fresh, tribe_owners_reload_fresh_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_owners_reload_fresh --lane N --retry 0 --no-build` (the host gives all but one member, `coop.store hold`,
  saves, loads again: the same owners on both games).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_owners_reload_fresh_off --lane N --retry 0 --no-build` (no tribe.owners in that save: the members are
  dealt anew).
* Regressions near it: tribe_owners_reload, tribe_owners_session, civ store tests, space_session.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (95e8a495); waiting for the night lab run.
