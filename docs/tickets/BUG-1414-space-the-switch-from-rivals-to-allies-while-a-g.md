---
id: BUG-1414
title: Space: the switch from Rivals to Allies while a guest is in an editor or an adventure credits the shared total back to the host as the guest's correction (audit SPC-2)
status: fixed
kind: bug
stage: Space
reported: 2026-11-22
source: audit 0.7.15.5 SPC-2 (out/research/ext-audit-verify-civ-spc.md)
role: guest
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-space
commits: 0d07ae00
tests: space_merge_away, space_merge_away_off
signature:
docs:
related: BUG-1029, BUG-0813, BUG-0231
---

## Report
External static audit of 0.7.15.5, finding SPC-2 (verified REAL, money dupe). Not seen at runtime, found by code reading.
The host switches Rivals -> Allies while a guest is in an editor or an adventure entered from Space. Back in Space the
host's game gets its own money (and the other guests' budgets) a second time.

## Root cause
Inferred from code (rel/0.7.16 5376d388), confidence high for the path, medium for "GetPlayerEmpire is non-null in an
editor entered from Space" (the audit's runtime condition; the second path below needs no such condition):
* space_rivalry.cpp host_apply A_ALLIES needs only the host's `money_ptr()`; it credits each guest's last K_MONEY and
  bumps the merge seq.
* The guest's on_state sets `g_merge_pending`, `g_merge_credit` and calls apply_pending_money(), which returns at once
  while `money_ptr()` is null (`!spaceext::in_space()`, BUG-0813: editor / adventure).
* shared.cpp on_message MSG_RES: `own_budget` is false now, get_now -> space_money() has no in_space() check, so the
  shared total T is written into the guest's empire money. Also without that: frame_receive runs before
  frame_stage_ticks, so a MSG_RES in the first Space frame back does the same.
* Back in Space, apply_pending_money computes `d = *m - g_merge_credit` = T - credit; the BUG-1029 guard passes
  (`g_win.known` survives an editor), K_MERGE_FIX d -> the host adds it.

## Fix
shared.cpp holds a guest's shared Space totals (MSG_RES of the Space money) while `rivalry::merge_pending()` is true:
the guest's game keeps its own budget until apply_pending_money() sent the correction (its own budget's change since
the host's credit). The host sends its total again within 2 s (the safety net of shared::tick), so nothing waits long.
`rivalry` status shows `merge_pending=` and `merge_held=` (totals held). Switch `coop.fix space.merge_hold_totals off`:
taken at once (0.7.16). No wire change.

## Verification
open, waiting for the lab run.
`coop_space_merge_away.py` (suite `space_merge_away`, twin `space_merge_away_off`, fixture space): Rivals, the guest
earns 7000 and the host spends 1000, the guest acts as outside Space (`coop.space away 1`), the host switches to Allies,
6 s later the guest is back: both games' Space money (cEmpire +0xD0) must be host budget + guest budget and stay.
Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=space.merge_hold_totals (host = its budget twice + the guest's).

- 2026-11-23 (0.7.16.1 final check): space_merge_away PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened.
- 2026-11-22: fix and test on fix/w63-space (no lab run yet: lanes busy with the 0.7.16 final check).

- 2026-11-22: renumbered from BUG-9299 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9299.
- 2026-11-23: released in 0.7.16.1.
