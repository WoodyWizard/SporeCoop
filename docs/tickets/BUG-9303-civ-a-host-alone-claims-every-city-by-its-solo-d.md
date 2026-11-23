---
id: BUG-9303
title: Civ: a host alone claims every city by its solo deal, so a joiner gets no city of its own (CIV-6)
status: open
kind: bug
stage: Civ
reported: 2026-11-22
source: audit
role: host
versions: 0.7.15 and later (since E1, BUG-1153)
fixed_in:
branch: fix/w63-civ
commits:
tests: civ_solo_deal, civ_solo_deal_off, civ_owners_session
signature:
docs: docs/06-testing.md#testing-civ-audit-w63
related: BUG-1153, BUG-0950, BUG-0875
---

## Report
External static audit of 0.7.15.5, item CIV-6 (verified in out/research/ext-audit-verify-civ-spc.md). Hits a host who
loads a Civ save with several cities and waits for friends: the joiner gets only the shared capital. With one city
(civ_npc, a fresh Civ) nothing changes, which is why civ_owners_session passes. Owner's rule (2026-11-21): the host
keeps the cities it really had before a guest arrived (owners recorded in the store; captures, gifts, purchases), but
gets nothing for free: no claim from playing alone; unowned cities stay for the split at the join.

## Root cause
* (A) Code: a host is `connected()` alone (net/session.h: `role_ == Role::Host ||`), so host_tick runs solo:
  `host_assign_cities([0])` gives every city to 0 and `claims_refresh` (every 2 s) stamped `key_of(0)` ("host") on each.
  At a join `claimed()` returned the host for every non-capital city (`claims::holder`), the guest was in `first` with no
  free city: only the shared capital.
* (B, console only) `coop.store adopt` renamed the claim keys but not `g_city_owner` / the vehicles' owners;
  host_assign_cities keeps an owned city unless resplit, and claims_refresh wrote the old holder's (the host's) key back
  2 s later. Inferred from code, not run.

## Fix
civ_host.cpp: (A) while the host plays alone (fewer than 2 players) claims_refresh keeps the claim a city has and gives
none from the deal (`civlogic::city_claim`, tests/civ_tests.cpp); captures, gifts and purchases still claim at the
event, the store's claims come back as before; with 2+ players the deal claims as before. Switch
`coop.fix civ.solo_claims off` (0.7.16). Note: saves already played alone with 0.7.15 / 0.7.16 hold the host's stamped
claims in the store and keep them (the owner's rule: a recorded owner stays). (B) the store's adopt callback gives the
adopted player its claimed cities (not the capital) and vehicles in the owner table at once (`adopt_owners`); switch
`coop.fix civ.adopt_owners off`. `coop.civ tables`: `solo_claims: fix= unclaimed= adopt_fix= adopt_owned=`; test command
`coop.civ claims forget <city>` (host). No wire change.

## Verification
open, waiting for the lab run. `coop_civ_solo_deal.py` (suite `civ_solo_deal`, civ_npc): the guest leaves, the host
alone buys cities A and B, A's claim is forgotten (a save played without co-op), 7 s alone, the guest joins; PASS: A is
the joiner's on both games, B still the host's. Twin `civ_solo_deal_off` (SPORE_FIX_OFF=civ.solo_claims): A stays the
host's, must FAIL. Regression: civ_owners_session, civ_owners_reload. (B) has no lab test (needs a guest with a new
token; the rule is in tests/civ_tests.cpp).

## History
- 2026-11-22: opened.
- 2026-11-22: fix on fix/w63-civ (switches civ.solo_claims, civ.adopt_owners; test civ_solo_deal and its twin); lab run pending.
