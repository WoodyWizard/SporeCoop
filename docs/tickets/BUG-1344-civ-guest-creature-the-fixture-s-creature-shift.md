---
id: BUG-1344
title: civ_guest_creature: the fixture's creature shift leaves host creatures 40-45 m from the place (shift radius 40, check 45)
status: open
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: rel/0.7.16
fixed_in:
branch: fix/w60-tribe
commits:
tests: civ_guest_creature
signature:
docs:
related: BUG-0970, BUG-0973
---

## Report
The 0.7.16 final check (lab 20261120-041304, lane 6): civ_guest_creature "FAIL the host has no creature within 45 m of
the guest's (40.4 m)"; every check of its subject (BUG-0970: the host takes the guest's attack order on a creature it
does not have there) passed in the same run. The same precondition failed on fix/w49-civ (20261113-040703, 40.8 m).

## Root cause
Test (its setup), not the mod. ✔ (code: civ_debug.cpp `creatureshift <x y z> <r> <d>`) the test moved every host
creature within r = 40 m of the place d = 80 m aside along one ground direction: a creature 40-45 m away is not moved,
and a moved one on the far side of the circle ends 40 m from the place (80 - 40). The check right after wants none
within 45 m, so it failed whenever the host's herd had a member in that ring (wild creatures are not synced here:
the test runs with npc.civ_wild off, BUG-0973, and the two herds differ by a few metres).

## Fix
The test moves every host creature within 60 m by 140 m: a moved one ends at least 80 m away, one not moved is over 60 m
away; the check (none within 45 m) is unchanged and now holds by construction. Comment in the test.

## Verification
no-fail-run: a test setup fix (the FAIL was the precondition: lab 20261120-041304, 20261113-040703).
Open, waiting for the lab run: civ_guest_creature must PASS (the nearest host creature printed over 45 m); `--off`
(coop.fix civ.creature_target off, run by hand) must still FAIL on the rejected orders.

## History
- 2026-11-20: opened.
