---
id: BUG-0417
title: Tests: npc_walk's flee phase hits members of the host's own family, which hit the host back (monitor posse_damage gate)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab 20261101-153010 npc_walk, lane 7 (fix/w33-proxy-ground)
role: host
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w35-monitor-gates
commits: 4293fc9, 36a86b6
tests: npc_walk, npc_walk_tribe (tools/tests/coop_npc_walk.py)
signature:
docs: docs/06-testing.md#testing-monitor-w35
related: BUG-0390, BUG-0374
---

## Report
out/lab/20261101-153010-fix-w33-proxy-ground npc_walk (lane 7) FAILED only on the monitor gate `posse_damage: host: player 0
hit by its own party (family, posse=1) for 1.0 hp (s 64.884)`: 14 hits of 1.0 hp from s 64.9 on, right after the test's
flee phase (`coop.npc hosthit <id> 0.5` on two of its walkers, the NPCs nearest the guest). All the test's own checks
passed. Also with this branch's fixes off (`SPORE_FIX_OFF=npc.walk_ground,npc.proxy_ground`: out/lab/20261101-163228,
the same gate at s 73.5) and in out/lab/20261101-161258 npc_walk#1, #2; fix/w33-null-call's 20261101-141648 npc_walk
(other walkers, #5 and #4 hit) passed.

## Root cause
Decided (fix/w35-monitor-gates): the test. Its walkers were the 4 host NPCs whose proxies were nearest the guest
(within 30 m), and next to the two avatars those were exactly the host avatar's family: with NPC_WALK_PARTY=1 (the
old choice) the test printed `walkers [1, 2, 4, 5] ...; host pack / family [1, 2, 4, 5]` (the failing runs had the
same ids, `walkers [2, 4, 1, 5]`). The flee phase's `coop.npc hosthit` is TakeDamage with the host's own avatar as
the attacker on #1 and #2; the family then fought the host (14 `damage: 0 hit by family owner=npc posse=1
amount=1.0` from s 103.6). That is the game's answer to the host attacking its own party, a hit the test forced; a
guest's hit on the host's family is dropped (npc.party_peace, BUG-0374) and no hit path of the mod is involved, so
it is not a bug players meet. Not the walk switches (it failed with them off too).

## Fix
Test only (no game code change, so no switch; BUG-0390's rule): coop_npc_walk.py reads the host's pack / family
(`coop.npc list` tags) and walks and hits NPCs outside them, taken from up to 100 m (within 30 m of the guest there
were only the family's 4: the first version of this fix had no walkers at all and still passed, so the test now
fails without 2 walkers). It prints the party and whether a walker is in it. NPC_WALK_PARTY=1: the family first
(the test before).

## Verification
* FAIL (the test before, NPC_WALK_PARTY=1): npc_walk out/lab/20261101-174548 (walkers [1, 2, 4, 5] = the family;
  `gate monitor: 14 on host (posse_damage)`); also out/lab/20261101-163228 and 20261101-153010 (fix/w33-proxy-ground).
* PASS: npc_walk 2/2 out/lab/20261101-183139 (walkers [20, 18, 21, 19] and [29, 41, 30, 25], none of the family
  [1, 2, 4, 5] / [32, 33, 35, 36]; flee windows 770 / 346, no `posse=1` damage line; matched 98 % / 93 %, gait 96 % /
  100 %). The first version (no walkers, out/lab/20261101-175700) passed without walking anything: not counted.
  npc_walk_tribe: the Tribe host lists no family (`host pack / family []`), its walkers are the ones before.

## History
- 2026-11-01 (fix/w35-monitor-gates): decided: the test hit the host's family with the host's avatar; walkers outside the party.
- 2026-11-01: opened.
- 2026-11-02: released in 0.7.3.4.
