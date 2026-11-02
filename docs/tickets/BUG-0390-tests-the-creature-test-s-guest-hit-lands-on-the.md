---
id: BUG-0390
title: Tests: the creature test's guest hit lands on the host's family (hp 10 NPC), which npc.party_peace drops: hits applied 0 -> 0
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab runs on fix/w30-tribe-walk after merging main 15539ad
role: host
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w32-tribe-sync
commits: 08ae615
tests: creature (tools/tests/coop_two_instances.py)
signature:
docs:
related: BUG-0374
---

## Report
`creature` (coop_two_instances.py, M2: the guest hits the first spawned NPC proxy with `coop.npc hit <id> 1`
and expects the host to apply it within 0.5 s) failed 2 of 4 runs on fix/w30-tribe-walk after merging
main 15539ad: `npc #1: host hp 10.0 -> 10.0, hits applied 0 -> 0` (out/lab/20261101-094936,
out/lab/20261101-101600 creature#2). The passing runs hit an NPC of hp 5-7.5. The host's avatar has
hp 10/10: an NPC of hp 10 is most likely a member of the host's family (its herd).

## Root cause
Observed (lane 7, out/lab/20261101-123339): the host's `coop.npc list` (now tagged ` family` / ` pack`) had
4 of the guest's 20 spawned proxies in the avatar's herd (ids 1, 2 and two more), and in 2 of 5 runs the
first proxy the guest listed, the one the test hit, was #1, a family member. Before that, inferred: since BUG-0374 (`npc.party_peace`, npc.cpp MSG_DAMAGE_REQ) the host drops
a partner's hit on its pack or family (`creature_hp::drop_partner_hit`: pack bit, or the NPC in the
avatar's herd). The test picks `spawned[0]` of the guest's proxy list, which can be such a member; the
hit is then dropped by design. Not a change of fix/w30-tribe-walk (no NPC or damage code changed there).
Check: `coop.status` on the host shows the creature_hp counters (partner hits dropped) after the hit.

## Fix
Test only (no game code change, so no switch): `coop.npc list` on the host tags the avatar's herd
(` family`, npc.cpp list); coop_two_instances.py reads the host's pack / family ids and hits the first
spawned proxy outside them (it prints whether the first listed proxy was one of them).

## Verification
* FAIL (the test before): out/lab/20261101-094936, out/lab/20261101-101600 creature#2 (fix/w30-tribe-walk
  worktree), "hits applied 0 -> 0" on an hp 10 NPC.
* PASS: out/lab/20261101-123339 creature 5/5 (fix/w32-tribe-sync, lane 7); the first listed proxy was a
  family member in runs #3 and #5, the hit went to #3 / #6 (hp 6.0 -> 5.0, hits applied 0 -> 1).

## History
- 2026-11-01: fix/w30-npc-walk: also with an hp 5 NPC (out/lab/20261101-131942 creature#2); with that branch's walk switches off
  1 of 3 passed (20261101-132700), on 2 of 3 (20261101-132708): not the walk driving.
- 2026-11-01: opened.
- 2026-11-01 (fix/w32-tribe-sync): the test hits an NPC outside the host's pack / family; 5/5 on lane 7.
- 2026-11-02: released in 0.7.3.4.
