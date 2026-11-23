---
id: BUG-1487
title: Space: false raid notices for own-star mirrors (and extra raids with 3-4 players) (SPC-13)
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: e9c0dd3d, 37d9240d
tests: space_own_mirror, space_own_mirror_off_raid
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0543, BUG-0552, BUG-0840
switch: space.own_mirror_raids
---

## Report
External static audit of 0.7.15.5, item SPC-13 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: spacecoop.cpp h_SetState keeps "raids on my own colonies only" by comparing
the star's owner with my empire; a partner's own-star mirror colony (space_allies add_planet_own, BUG-0543) is in a
star of mine, so it passes. space_allies helper_raids starts a raid of my own there (StartRaid, owner = the star's
owner) while I am on it and the owner is not: its SetState 3 / 5 went through as "my colony": K_RAID to everybody
("<me>'s colony is raided / safe" for the owner's colony), the contracts' raid listener, and on 5 `add_money(2000)` if a
partner was at my place (with 3+ players a bonus on top of the owner's own).

## Fix
In the raid branch of h_SetState a raid on `spaceext::allies_own_mirror(planet)` returns before the notice, the
listener and the bonus (counted `own_mirror_raids`); the owner's game tells its own raid, the helper's win still goes
through space_allies' K_DEFENDED. Switch `coop.fix space.own_mirror_raids off`: 0.7.16.1.

## Verification
open, waiting for the lab run.
`coop_space_own_mirror.py` (suite `space_own_mirror`, twin `space_own_mirror_off_raid`): the guest's colony in the home
system is an own-star mirror in the host's game; a raid starts on it there (`allies raid`, the game's StartRaid as the
helper's): over 8 s the guest gets no "the host's colony is raided" (raids_told), the host counts no raid on its colony.
Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=space.own_mirror_raids.
pending night run: space_own_mirror space_own_mirror_off_raid (1 lane, about 10 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
