---
id: BUG-1430
title: TR-6: Tribe: after a world change a present player with no claim gets no members until the next join or leave
status: open
kind: bug
stage: Tribe
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in:
branch: fix/w63-life
commits: 0e1f881a, 8b8ed93c, 39f2f060
tests: tribe_owners_reload_back (coop_tribe_owners_reload.py --back); twin tribe_owners_reload_back_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-1154, BUG-1429
---

## Report
External static audit of 0.7.15.5, item TR-6 (out/research/ext-audit-verify-cr-tr.md, narrow: the audit's join case is false): after a world change a present player with no claim on a world whose members are all claimed gets no members until the next join or leave. TR-1 hid it (its claims were dropped first); fixing TR-1 exposes it.

## Root cause
Inferred from code (tribe_host.cpp host_tick): the first tick after a world change runs the resplit with `g_host.members` empty (cleared in on_world_change), so `member_balance` sees no owners; then the adopt loop deals the members by their claims (tribe_logic new_owner). A present player without a claim gets none, and `split_for == players` blocks any further balance until the roster changes.

## Fix
The balance of a resplit runs after the adopt loop (with the entries of this tick). Switch `coop.fix tribe.claims_balance off`: before the adopt loop as 0.7.16. No message changed.

## Verification
Open, waiting for the lab run. PASS expected: tribe_owners_reload_back (the guest gives all its members to the host, the host saves and loads again: the guest owns its share, at least one and half of the members, on both games). FAIL expected: tribe_owners_reload_back_off (SPORE_FIX_OFF=tribe.claims_balance: the guest owns no member after the load).

## History
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.

- 2026-11-22: renumbered from BUG-9315 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9315.
