---
id: BUG-1267
title: Test cell_attack_spike/jaw: the attacker finds no creature within 40 m and the test fails on its own control
status: fixed
kind: test
stage: Cell
reported: 2026-11-18
source: final check 0.7.15.2
role: 
versions: 
fixed_in: 0.7.15.2
branch: rel/0.7.15.2
commits: 71aa3eb7
tests: cell_attack_spike, cell_attack_jaw
signature:
docs:
related: BUG-1243
---

## Report
Final check of 0.7.15.2: cell_attack_spike failed in 20261118-144132 (guest round) and 20261118-164423 (host
round) with "the <attacker>'s own cell played attack / eat animations ([])". The log shows `0 swims`: `coop.pool see
40` found no creature near the attacker, so no attack happened at all; the BUG-1243 checks themselves passed.

## Root cause
Test precondition: the fixture's spot sometimes has no creature within 40 m (or the first round ate them).

## Fix
~ superseded (2026-11-20): see BUG-1355. `contact()` in coop_cell_attack_anim.py looks for a creature within 40, then 120, then 300 m; when the attacker
still found none (0 swims) its checks are left out and the test ends INCONCLUSIVE instead of FAIL.

## Verification
no-fail-run: test-only change (the precondition of the test, not the mod)

## History
- 2026-11-18: opened; fixed in the test. Lab 20261118-165138: spike and jaw twice each: 3 OK (all checks pass when the attack happens), 1 INCONCLUSIVE (no creature even within 300 m on that run).
- 2026-11-18: released in 0.7.15.2.
- 2026-11-20: CORRECTION (fix/w60-misc): `coop.pool see N` takes a row count of on-screen cells, not a radius: the 40 / 120 / 300 m tries were one on-screen search; the guest rounds of the 0.7.16 final check (20261120-041304) were INCONCLUSIVE with no creature on the guest's screen. BUG-1355 swims to the nearest creature of the pool.
