---
id: BUG-1506
title: Creature: a guest's spend owed while the host had no avatar is applied in another Creature world (CORE-14)
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: fa48934a
tests: tools/tests/coop_dna_spend_owed_world.py (dna_spend_owed_world, twin dna_spend_owed_world_off)
signature:
docs:
related: BUG-0233
switch: creature.spend_owed_world
---

## Report
External static audit of 0.7.15.5, item CORE-14 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (session_glue.cpp the DNA route, coop.cpp host_away_tick): with one shared pool a guest's
spend that reached the host while it had no creature was added to `g_dna_spend_owed` and taken off in any Creature world
with an avatar, with no world tag (unlike `g_dna_owed`, BUG-0233): a host that loaded another Creature world meanwhile lost
the guest's spend there. Only with all.shared_dna on (own wallets do not owe).

## Fix
`g_dna_spend_owed_world` (the stage world when the first spend was owed); coop.cpp takes the spend off through
dna_route::owed (Pay in that world, Clear in another stage or world, logged, `spend_dropped=` in coop.status). Switch
`creature.spend_owed_world` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: dna_spend_owed_world, dna_spend_owed_world_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run dna_spend_owed_world --lane N --retry 0 --no-build` (shared DNA, the host in the menu, the guest spends 30, the
  host loads Cadian: its DNA there is the save's).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run dna_spend_owed_world_off --lane N --retry 0 --no-build` (30 less in Cadian).
* Regressions near it: dna_own_shared, dna_own_spend.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (fa48934a); waiting for the night lab run.
