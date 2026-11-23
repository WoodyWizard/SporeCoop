---
id: BUG-1429
title: TR-1: Tribe: the member claims follow the owners before the members are adopted: who owns which member is lost on a reload, a join or coop.store adopt
status: open
kind: bug
stage: Tribe
reported: 2026-11-22
source: audit
role: host
versions: 0.7.16.1
fixed_in:
branch: fix/w63-life
commits: 0e1f881a, 39f2f060
tests: tribe_owners_reload (coop_tribe_owners_reload.py), tribe_owners_reload_shared; twin tribe_owners_reload_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-1154, BUG-1430
---

## Report
External static audit of 0.7.15.5, item TR-1 (out/research/ext-audit-verify-cr-tr.md): who owns which tribe member (bought children, gifts, the guest's chief) is lost on a reload with a guest present (the game over's reload, any load in a session), at a join, and after `coop.store adopt`. BUG-1154's ticket says the game over's reload was not run in the lab.

## Root cause
Inferred from code (tribe_host.cpp host_tick): `member_claims_tick` ran first, before the adopt loop and the resplit. Its 2 s follow keeps a claim only while its player is absent, else takes the member entry's owner, else drops it. (a) after a world change `on_world_change` clears the member entries and the match flag but not `g_mclaims_at`: on the first tick the claims are matched from the store and the follow runs at once with no entries (hm null): every present player's claim (the host's "host" too) was dropped, then the adopt loop dealt in turns. (b) at a join the follow (due about 1 tick in 17) saw the newcomer present while its members were still held by the host and rewrote their claims to "host" before the resplit. (c) `coop.store adopt` renames the claim keys only; no resplit follows (roster unchanged) and the next follow rewrote them to the holder's key.

## Fix
The follow (`member_claims_follow`) runs after the adopt loop, the resplit and the sweep of the same tick; a claimed member without an entry keeps its claim; the adopt callback clears `split_for` so the renamed claims are dealt to their player. Switch `coop.fix tribe.claims_order off`: as 0.7.16. No message changed.

## Verification
Open, waiting for the lab run. PASS expected: tribe_owners_reload and tribe_owners_reload_shared (the host gives all but one member to the guest, saves, `coop.menu`, `coop.load` of the same slot with the guest in the session: the same owners on the host and in the guest's game). FAIL expected: tribe_owners_reload_off (SPORE_FIX_OFF=tribe.claims_order: dealt in turns after the load). Not covered by a lab test: the join case (b) (a 1-in-17 tick window) and (c) (`coop.store adopt` needs a player with a new token); both are the same order fix. Owners do not depend on all.shared_dna (checked by the _shared run).

## History
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.

- 2026-11-22: renumbered from BUG-9314 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9314.
- 2026-11-23: the join case (b) now has a lab test (BUG-1450, fix/w67-chief): tribe_owners_rejoin with `coop.tribe claims_at_join 1` (PASS x3, out/lab/20261123-095717) and the twin tribe_owners_rejoin_off (SPORE_FIX_OFF=tribe.claims_order, FAIL x3, same run).
