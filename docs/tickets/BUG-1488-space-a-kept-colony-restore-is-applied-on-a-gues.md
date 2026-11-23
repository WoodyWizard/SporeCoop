---
id: BUG-1488
title: Space: a kept colony restore is applied on a guest's own Space save (part c) (SPC-14)
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: e0c93959, 37d9240d
tests: space_kept_own_save, space_kept_own_save_off
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0195, BUG-0709, BUG-0816, BUG-1151
switch: space.kept_own_save
---

## Report
External static audit of 0.7.15.5, item SPC-14 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: space_map.cpp applies the host's K_KEPT list (the guest's own colonies of the
co-op galaxy) once `in_space() && world_epoch() != g_win.join`, with no own_world::space_other() check. A guest that
joined from the menu (join = 0) or reloaded and then plays a Space save of its own sends its roster, gets the host's
K_KEPT and colonize_own made those planets its colonies in its own save (with BUG-1151 also NPC stars), which it then
saves. Parts a (a failed list of a tick that waits is not sent: log noise) and b (the 10 s resend re-applies
idempotently) of the audit item are harmless and not changed.

## Fix
The apply waits while `own_world::space_other()` (the list stays, `own_held` counts the frames) and runs in the host's
world. Switch `coop.fix space.kept_own_save off`: 0.7.16.1. Status: `coop.space map kept` own_held=.

## Verification
open, waiting for the lab run.
`coop_space_kept_own_save.py` (suite `space_kept_own_save`, twin `space_kept_own_save_off`): the guest's colony in the
home system is kept by the host; the guest leaves, joins again from the menu without the host's world and loads a copy
of the share save: over 30 s the planet stays free in that save (tech < 5) and the list waits. Expected: PASS with the
fix; FAIL with SPORE_FIX_OFF=space.kept_own_save.
pending night run: space_kept_own_save space_kept_own_save_off (1 lane, about 16 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
