---
id: BUG-1450
title: Tribe: after a guest's quick rejoin the host's chief (the game's leader with the staff) became the guest's member (claims refresh before the resplit, balance picks the chief)
status: open
kind: bug
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16
fixed_in:
branch: 
commits:
tests: tools/tests/coop_tribe_looks.py (tribe_held_item: "the host's chief #N is still the host's member"), tribe_owners_session
signature:
docs:
related: BUG-1154, BUG-1449
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): tribe_held_item, after the guest's rejoin (`coop join` again): the game's leader #1 with the staff is labelled
"Bob's chief" on both screens (key frames 04-06), the guest's arrow says "Alice (chief) 31 m", the manifest's
chief_rows "no chief" (no owner=0 leader=1 row on the host). At the start #1 was "Alice's chief"; in runs 2 and 3 it
was still the host's after the rejoin. Breaks BUG-1154 ("members keep their owners across joins and leaves").

## Root cause
Inferred from the code, not yet seen in a log line (tribe_host.cpp): `member_claims_tick` runs before the resplit;
when its 2 s refresh lands in the host tick in which the guest is back, `tpresent(guest key) >= 0` skips the "keep"
branch and the guest's claims are rewritten to the interim owner (the host); the resplit then gives the guest nothing
and `member_balance` (tribe_logic::balance: the first plain member in member order, the chief included) hands it #1.
About 0.12 per rejoin (the refresh window), consistent with a first sighting. Code unchanged since 0.7.15.5: not a
0.7.16 regression.

## Fix
Not in 0.7.16 (owner rule: mod bugs that are not regressions wait): planned for 0.7.16.1. Ideas: the claims refresh
after the resplit (or not in the tick of a join), and balance never moves the leader.

## Verification
Open. The test: tribe_held_item's new check "the host's chief #N is still the host's member" (BUG-1449) fails when it
happens.

## History
- 2026-11-23: opened (fix/w65-night triage); for 0.7.16.1.
