---
id: BUG-1483
title: Space Rivals: the Rivals money traffic changes the money of a guest playing its own Space save (SPC-4)
status: fixed
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w77-space
commits: 48cdcb66, 37d9240d
tests: space_own_rivals, space_own_rivals_off_budget
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0709, BUG-0816, BUG-1485
switch: space.rival_own_save
---

## Report
External static audit of 0.7.15.5, item SPC-4 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: space_rivalry.cpp had no `own_world::space_other()` check. A guest that plays a
Space save of its own while connected (supported: BUG-0709, BUG-0816) in a Rivals session: the new world makes
`g_win.known` false, the tick sends K_ASK every 2 s, the host answers with its kept budget (or a newcomer's 0) and
K_SET `add_money(v - *m)` set the own save's money to it; then K_MONEY reported the own save's money as the Rivals
budget and a later split or merge applied the co-op correction to the own save.

## Fix
`own_save()` (a guest, `own_world::space_other()`): no K_ASK, K_SET ignored, no K_MONEY report, a pending split or
merge waits (applied once the guest is back in the host's world, where the BUG-1029 rule decides). The host keeps the
co-op budget. Switch `coop.fix space.rival_own_save off`: 0.7.16.1. Status: `rivalry` own_save= own_save_skips=.
No wire change.

## Verification
open, waiting for the lab run.
`coop_space_own_rivals.py` (suite `space_own_rivals`, twin `space_own_rivals_off_budget`, fixture space): Rivals, the
guest earns 8234, offers spice to the host's colony, then loads a copy of the share save under another name: its
money there must not become the co-op budget (and stays while the host buys the offer). Expected: PASS with the fix;
FAIL with SPORE_FIX_OFF=space.rival_own_save (the K_SET makes it the co-op budget).
pending night run: space_own_rivals space_own_rivals_off_budget (1 lane, about 16 min).
0.7.17 release lab: PASS: space_own_rivals (lab 20261124-020506); FAIL (fix off): space_own_rivals (lab 20261123-125807).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
- 2026-11-24: released in 0.7.17.
