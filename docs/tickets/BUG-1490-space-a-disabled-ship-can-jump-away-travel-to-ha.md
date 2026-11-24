---
id: BUG-1490
title: Space: a disabled ship can jump away (travel_to has no disabled check) (SPC-16)
status: fixed
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w77-space
commits: e9c0dd3d, 7c5e6258, 37d9240d
tests: space_down_jump, space_down_jump_off
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0063, BUG-1045
switch: space.down_no_jump
---

## Report
External static audit of 0.7.15.5, item SPC-16 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: jump(), travel() and teleport_in_galaxy() refuse while `g_down` (the disabled
ship), but travel_to() itself does not. Two callers went to it directly: the `jumpto` command that the "Jump there"
button of a co-op contract's row uses (space_contracts.cpp jump_to, the button shown with no disabled check), and
travel_to_star, the in-system step of the finale's "Fly to" that runs up to 30 s after its own disabled check
(space_finale fly_tick). A disabled ship escaped its rescue window by a jump.

## Fix
travel_to_star and `jumpto` (unless `force`, the tests' old jump) refuse with "the ship is disabled" while disabled
(`down_jumps_refused`). Switch `coop.fix space.down_no_jump off`: 0.7.16.1. Test seam: `coop.space contracts jump
<star>` (the row button's function).

## Verification
open, waiting for the lab run.
`coop_space_down_jump.py` (suite `space_down_jump`, twin `space_down_jump_off`): the guest queues "Fly to" with its step
6 s later, takes a lethal hit next to the host (disabled, nobody repairs), presses a contract row's "Jump there": it
is refused and the ship stays in its home system over 15 s. Expected: PASS with the fix; FAIL with
SPORE_FIX_OFF=space.down_no_jump.
pending night run: space_down_jump space_down_jump_off (1 lane, about 8 min).
0.7.17 release lab: PASS: space_down_jump (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
- 2026-11-24: released in 0.7.17.
