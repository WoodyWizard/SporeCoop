---
id: BUG-1505
title: World share: a guest leaving mid-transfer or a failed share leaves the ground reload due (CORE-13)
status: open
kind: desync
stage: All
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: afef55d1
tests: tools/tests/coop_tribe_reload_any.py (tribe_reload_any, twin tribe_reload_any_off; 3 players)
signature:
docs:
related: BUG-1054
switch: world.reload_after_any
---

## Report
External static audit of 0.7.15.5, item CORE-13 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (world.cpp tick): the host's reload after the live Creature -> Tribe change (BUG-0332) ran
only when a share succeeded with nothing pending after it. A guest that left with its share still pending (erased by the
leave loop) or a last share that failed left `g_reload_due` set (until a world change, BUG-1054) although other guests had
loaded the save: the host kept the live ground they do not have until a later share. Needs 3+ players.

## Fix
world.cpp: `host_reload_now()` (the reload, factored out); `g_reload_shared` notes that a share of the due reload went
out; after both loops the reload runs when nothing is pending any more and a share went out, whatever ended the last one.
Test seam `coop.worldfix failpeer <id>` (the next share to that peer fails once). Switch `world.reload_after_any` (off:
0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: tribe_reload_any, tribe_reload_any_off; 3 players on one lane).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_reload_any --lane N --retry 0 --no-build` (the reload armed, Bob's share goes out, Carol's fails: the
  host's game loads its share save and plays Tribe again).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_reload_any_off --lane N --retry 0 --no-build` (no reload).
* Regressions near it: tribe_host_reload_reset, tribe_village_ground.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (afef55d1); waiting for the night lab run.
