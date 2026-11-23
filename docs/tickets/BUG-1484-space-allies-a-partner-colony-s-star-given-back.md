---
id: BUG-1484
title: Space allies: a partner colony's star given back (restore_star) without rebuild_near: caught fault with the BUG-1144 signature (SPC-5)
status: open
kind: crash
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: 0cad8cc8, 37d9240d
tests: space_mirror_near, space_mirror_near_off
signature:
docs: docs/re/space.md#space-audit-0717, docs/re/space.md#space-near-stars
related: BUG-1144
switch: allies.apply_near_rebuild
---

## Report
External static audit of 0.7.15.5, item SPC-5 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high (the same mechanism as BUG-1144, which was seen at runtime): space_allies.cpp
apply() gives a mirror star back to its old owner (`restore_star`, usually nobody: +0x54 = -1) when the partner's
colony there is lost or blocked and nothing else of the mirror uses it, but only unapply() rebuilt the game's list of
owned stars nearby (cSimulatorSpaceGame +0x30 -> +0x7C). With that star in the list (the guest visited it), the NPC
visitor spawn 0x1009070 can pick it and call 0xC32810 on GetEmpire(-1) = null: the caught fault at 0xC32817.

## Fix
apply() notes when its loop gave a star back and then rebuilds the list (`rebuild_near()`, 0xFE6A50, as unapply()).
Switch `coop.fix allies.apply_near_rebuild off`: 0.7.16.1 (BUG-1144's own switch allies.near_rebuild off also
stops it). Status: `coop.space allies near` apply_star_back=.

## Verification
open, waiting for the lab run.
`coop_space_mirror_near.py` (suite `space_mirror_near`, twin `space_mirror_near_off`, fixture space): the guest's mirror
empire Zed (`allies fake`, the same apply()) has a colony in a free star S1, the guest visits S1 and flies home (the
game lists S1), then Zed's colony moves to another star: the game's list must hold no star without an empire, now and
over 20 s, and no fault. INCONCLUSIVE when the game did not list S1 after the visit. Expected: PASS with the fix; FAIL
with SPORE_FIX_OFF=allies.apply_near_rebuild (stale=1).
pending night run: space_mirror_near space_mirror_near_off (1 lane, about 12 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
