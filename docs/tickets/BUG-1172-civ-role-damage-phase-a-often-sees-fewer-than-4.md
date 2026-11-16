---
id: BUG-1172
title: civ_role_damage: phase A often sees fewer than 4 hits in its 30 s (no damage per hit), the test FAILs without a mod fault
status: fixed
kind: test
stage: Civ
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-civ
commits: d1a4d5c5
tests: civ_role_damage
signature:
docs:
related: BUG-0064, BUG-0430
---

## Report
civ_role_damage (tools/tests/coop_civ_role_damage.py) FAILs in its first phase: phase A (the guest without a role)
counts 0-3 hits on the NPC city in its 30 s, so its damage per hit is None and both checks fail ("A None"), while
phases M and B, with the vehicles at the city by then, count 18-20 hits each (26.0 military, 21.0 no role: the role
rule itself works). Seen on feat/w51-civ (lab 20261115-161514 lane 7 warm, 20261115-164954 lane 7 cold) and before
it on fix/w49-warm (lab 20261113-111705: 1 hit in A, 0.7.14 code), so not a change of this branch.

## Root cause
Inferred: the two vehicles are spawned at the player's city and ordered to the nearest NPC city when phase A starts;
their drive there takes most of PHASE (30 s) on a loaded lab, so A measures the drive, not the hits. The later phases
start with the vehicles at the city. Not verified with positions yet.

## Fix
Test only (d1a4d5c5): each phase's 30 s window starts with the first hit seen on the city (at most ARRIVE = 60 s after
the attack order), so phase A measures hits, not the drive. Thresholds unchanged (M >= 1.15 x A, B <= 1.10 x A).
Confirmed: in the PASS runs phase A's first hit came 25 s after the order, M's and B's after 0-1 s.

## Verification
- FAIL with the old test: lab 20261115-161514 and 20261115-164954 (A None), 20261113-111705 (fix/w49-warm).
- PASS with the change: lab 20261115-174639, 20261115-193323 (2 of 2).
- The test still catches BUG-0064: SPORE_LAB_FIXOFF="civ_role_damage=role.fix" FAIL in lab 20261115-194905 (A 21.0,
  B 26.0 = the old player's military role kept).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-civ): the phases measure from the first hit; PASS 3 of 3, role.fix off still FAILs.
