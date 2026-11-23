---
id: BUG-1355
title: Test cell_attack_mouth/spike: 'see 300' is a row count, not a radius: the attacker with no creature on its screen never swims to one
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in: 0.7.16
branch: fix/w60-misc
commits: 1c87e63c
tests: cell_attack_mouth, cell_attack_spike, cell_attack_jaw
signature:
docs:
related: BUG-1267, BUG-1243
---

## Report
Final check of 0.7.16 (lab 20261120-041304): cell_attack_mouth (lane 6) and cell_attack_spike (lane 3) INCONCLUSIVE "a
player found no creature to ram (no attack happened)": in both the host's round passed (28 / 39 swims) and the guest's
round had `0 swims`, "the guest found no creature to ram within 300 m".

## Root cause
Observed: the guest's status lines in both runs show `screen: ... creatures=0` from s 50 on (its pool holds 400-500
mirrored cells). `coop.pool see N` (cellsync_debug.cpp `see(int max)`) lists the N nearest cells that are ON THE
SCREEN: N is a row count, not a radius. BUG-1267's fix read it as a radius (40, 120, 300 m), so the "farther"
tries were the same on-screen search with more rows: an attacker with no creature on its screen never looked
farther and the round ended without an attack. CORRECTION of BUG-1267's "a creature within 40, then 120, then 300 m".

## Fix
coop_cell_attack_anim.py contact(): a creature on the screen is rammed as before (`see 60`, `swimto`); with none on the
screen the attacker swims towards the nearest creature of its own game's pool within 300 m (`coop.pool npcs 300`:
positions of every live creature cell, the guest's mirrors too; `coop.pool goto dx dy`, the click's target), so it
finds prey as a player does; the log names those distances. A round where the pool has no creature within 300 m stays
INCONCLUSIVE. Nothing the test checks changed.

## Verification
no-fail-run: test-only change (its precondition). Lab tonight: cell_attack_mouth, cell_attack_spike, cell_attack_jaw
PASS (both rounds with swims > 0). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_attack_mouth, cell_attack_spike and cell_attack_jaw PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in the test on fix/w60-misc.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
