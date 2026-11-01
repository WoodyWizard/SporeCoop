---
id: BUG-0417
title: Tests: npc_walk's flee phase hits members of the host's own family, which hit the host back (monitor posse_damage gate)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab 20261101-153010 npc_walk, lane 7 (fix/w33-proxy-ground)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
out/lab/20261101-153010-fix-w33-proxy-ground npc_walk (lane 7) FAILED only on the monitor gate `posse_damage: host: player 0
hit by its own party (family, posse=1) for 1.0 hp (s 64.884)`: 14 hits of 1.0 hp from s 64.9 on, right after the test's
flee phase (`coop.npc hosthit <id> 0.5` on two of its walkers, the NPCs nearest the guest). All the test's own checks
passed. Also with this branch's fixes off (`SPORE_FIX_OFF=npc.walk_ground,npc.proxy_ground`: out/lab/20261101-163228,
the same gate at s 73.5) and in out/lab/20261101-161258 npc_walk#1, #2; fix/w33-null-call's 20261101-141648 npc_walk
(other walkers, #5 and #4 hit) passed.

## Root cause
Inferred: the walkers can be members of the host avatar's own herd (family, `coop.npc list` ` family`); the test's
hosthit makes the host's avatar the attacker, so its family member fights back, a hit the monitor counts as the party
hitting its player. Fix direction: npc_walk picks walkers outside the family and pack (as BUG-0390 did for the
creature test), or the rule allows a hit back after the player's own attack.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
