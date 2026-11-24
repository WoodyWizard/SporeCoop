---
id: BUG-1450
title: Tribe: after a guest's quick rejoin the host's chief (the game's leader with the staff) became the guest's member (claims refresh before the resplit, balance picks the chief)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16, 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w67-chief
commits: fd47f96c, b2cd3f49, 7f840a0a, d14d7ff7
tests: tribe_owners_rejoin, tribe_owners_rejoin_back (coop_tribe_owners_rejoin.py); twins tribe_owners_rejoin_off, tribe_owners_rejoin_back_off; tribe_held_item (coop_tribe_looks.py: "the host's chief #N is still the host's member"); tests/tribe_tests.cpp balance_leader
signature:
docs: docs/re/tribe.md#tribe-balance-leader
related: BUG-1154, BUG-1429, BUG-1449
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): tribe_held_item, after the guest's rejoin (`coop join` again): the game's leader #1 with the staff is labelled
"Bob's chief" on both screens (key frames 04-06), the guest's arrow says "Alice (chief) 31 m", the manifest's
chief_rows "no chief" (no owner=0 leader=1 row on the host). At the start #1 was "Alice's chief"; in runs 2 and 3 it
was still the host's after the rejoin. Breaks BUG-1154 ("members keep their owners across joins and leaves").

## Root cause
Confirmed from the code; the log has no claims line (0.7.16 logs none) but fits only this path. In rel/0.7.16
(5376d388) `member_claims_tick` ran its 2 s follow first in `host_tick`, before the resplit. In the tick of the
guest's join (the follow due about 1 tick in 17) it saw the guest's key present while the host still held its members
(owner 0 since the leave) and rewrote their claims to "host"; the resplit gave every member to the host, and
`member_balance` (tribe_logic::balance: a player with no member gets total / players from the richest, plain members
first in member order) gave the guest 3 of 7, the leader #1 first (it is first in member order; a dealing in turns
would have given it to the host, players[0], so only the balance can have moved it). As the guest's lowest-id adult
it became "Bob's chief". Runs 2 and 3 missed the window. Two faults: (1) the follow before the resplit, which TR-1
(BUG-1429, `tribe.claims_order`) already fixed on rel/0.7.16.1 without a lab test of this join case; (2) the balance
moves the leader at all, which also happens with (1) fixed whenever a player with no member joins while the host is the
giver (a guest that gave all its members away and rejoins, a third player in a fully claimed session, TR-6's share
after a reload).

## Fix
`tribe_logic::balance` takes a `fixed(m)` predicate: those members never move, a giver with only fixed members above
the share gives no more; the host passes `is_leader` (Citizen_IsLeader). Switch `coop.fix tribe.balance_leader off`: as
0.7.16 (any member, the leader first). The join case itself is TR-1's order fix (`tribe.claims_order`). Test hook
`coop.tribe claims_at_join 1`: the follow is due in the tick of the next join (the window hit every time); `coop.status`
claims line `at_join=<armed>/<fired>`. Unit test: tests/tribe_tests.cpp balance_leader (with the 4-argument form, the
old behaviour, the leader goes first). Lab: tribe_owners_rejoin (the forced window: every member keeps its owner, the
leader the host's on both games), tribe_owners_rejoin_back (the guest gives all its members to the host and rejoins:
its share, never the leader); twins tribe_owners_rejoin_off (SPORE_FIX_OFF=tribe.claims_order,tribe.balance_leader,
the 0.7.16 code) and tribe_owners_rejoin_back_off (SPORE_FIX_OFF=tribe.balance_leader). Not changed: the dealing in
turns (no claims) could give the leader to a guest if it were not first in member order (not seen).

## Verification
fix/w67-chief, lanes 2 and 3, `SPORE_DETECTOR=20 python3 tools/lab.py run ... --retry 0`.
* PASS: out/lab/20261123-095717 tribe_owners_rejoin x3 (the hook fired in the join tick, at_join=0/1; the guest's
  2 of 3 members its own again, #1 the host's on both games). out/lab/20261123-095115 tribe_owners_rejoin_back x3 (the
  guest's share is #3, not the leader #1), tribe_owners_rejoin x3 (an earlier form without the gift), tribe_held_item
  x3: "the host's chief #1 is still the host's member" passed in all 3 at the start and after the rejoin (run 1 failed
  only its start grip checks while the host's chief held its staff gesture the whole window: BUG-1467, a test issue).
  out/lab/20261123-100123: tribe_owners_session, tribe_owners_reload, tribe_owners_reload_back PASS.
* FAIL (twins): out/lab/20261123-095717 tribe_owners_rejoin_off x3 (SPORE_FIX_OFF=tribe.claims_order: `changed:
  ['#3 1->0']`, the guest's claim lost in the join tick) and tribe_owners_rejoin_back_off x3
  (SPORE_FIX_OFF=tribe.balance_leader: `the host's chief #1 is still the host's member ... (owner 1)` on both games,
  the BUG-1450 symptom).
* make test: passed (tests/tribe_tests.cpp balance_leader).

## History
- 2026-11-23: opened (fix/w65-night triage); for 0.7.16.1.
- 2026-11-23: cause confirmed from the code; fixed on fix/w67-chief (tribe.balance_leader, the lab test of TR-1's join window).
- 2026-11-23: released in 0.7.16.1.
