---
id: BUG-0417
title: Tests: npc_walk's flee phase hits members of the host's own family, which hit the host back (monitor posse_damage gate)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: lab 20261101-153010 npc_walk, lane 7 (fix/w33-proxy-ground)
role: host
versions: 
fixed_in: unreleased
branch: fix/w34-walk-stable
commits: abc2b80
tests: npc_walk
signature:
docs:
related: BUG-0407, BUG-0390
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
Found in parallel on fix/w34-walk-stable (BUG-0407: 10 of 10 npc_walk runs on main failed on this gate, lab
20261101-143049; the host's log there: `damage: 0 hit by family owner=npc posse=1` after `hit #1`, a family member).
Confirmed: all 4 NPCs nearest the guest were family in lab 20261101-154000 npc_walk#1 (`family/pack [29, 30, 31, 32]`).
coop_npc_walk.py `family()` reads the ` family` / ` pack` tags of `coop.npc list`; walk_phase takes two walkers outside
them (up to 80 m away) and only those are hit (commit abc2b80).

## Verification
FAIL (the old selection): out/lab/20261101-143049 npc_walk 10 of 10 on the posse_damage gate. PASS: out/lab/20261101-154000
npc_walk#2-#10 (9 of 9 with two non-family NPCs hit) and out/lab/20261101-173719 npc_walk 5 of 5.

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w34-walk-stable (merged with main 53ce0d1).
