---
id: BUG-0390
title: Tests: the creature test's guest hit lands on the host's family (hp 10 NPC), which npc.party_peace drops: hits applied 0 -> 0
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: lab runs on fix/w30-tribe-walk after merging main 15539ad
role: host
versions: unreleased
fixed_in:
branch:
commits:
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
Inferred, not checked at runtime: since BUG-0374 (`npc.party_peace`, npc.cpp MSG_DAMAGE_REQ) the host drops
a partner's hit on its pack or family (`creature_hp::drop_partner_hit`: pack bit, or the NPC in the
avatar's herd). The test picks `spawned[0]` of the guest's proxy list, which can be such a member; the
hit is then dropped by design. Not a change of fix/w30-tribe-walk (no NPC or damage code changed there).
Check: `coop.status` on the host shows the creature_hp counters (partner hits dropped) after the hit.

## Fix
Open: the test should pick an NPC outside the host's herd (or accept a dropped hit on a party member and
try the next one).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: fix/w30-npc-walk: also with an hp 5 NPC (out/lab/20261101-131942 creature#2); with that branch's walk switches off
  1 of 3 passed (20261101-132700), on 2 of 3 (20261101-132708): not the walk driving.
- 2026-11-01: opened.
