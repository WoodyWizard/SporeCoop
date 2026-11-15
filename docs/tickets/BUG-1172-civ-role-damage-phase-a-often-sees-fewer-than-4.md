---
id: BUG-1172
title: civ_role_damage: phase A often sees fewer than 4 hits in its 30 s (no damage per hit), the test FAILs without a mod fault
status: open
kind: test
stage: Civ
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
