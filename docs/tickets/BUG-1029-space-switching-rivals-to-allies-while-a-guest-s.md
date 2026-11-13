---
id: BUG-1029
title: Space: switching Rivals to Allies while a guest's budget is not known yet merges the wrong money (the save's money or the shared total instead of its budget)
status: open
kind: desync
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-1
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-rivals-merge-unknown-budget
related: BUG-0192, BUG-0813, BUG-0422
---

## Report
Code audit for 0.7.14, finding SPC-1 (rated Critical there: "the shared treasury duplicated"). Not seen at runtime, found by code reading.
A guest that joins (or rejoins) a session in Rivals mode has no known budget until its game is in Space and the host answered its K_ASK with K_SET. If the host switches to Allies in that window (the guest is still loading the host's world, a minute or more, or the K_ASK round trip), the merge does not count that guest and the guest then sends its whole current money as the correction (K_MERGE_FIX), which the host adds to the shared money. What the players would see: the shared Space money after the switch is too high (or the rejoining guest's own Rivals budget is gone).

## Root cause
Observed in code (main efd2e0ba), confidence high for the path, medium for how much money is wrong:
* space_rivalry.cpp:469-473 on_state: the first K_STATE of the session sets g_have_state and g_merge_seen; g_budget_known stays false until K_SET (space_rivalry.cpp:587-599), which the guest asks for only in Space (tick, :687-689, every 2 s).
* Without g_budget_known the guest never sends K_MONEY (:692), so the host's g_budget has no entry for it.
* host_apply(A_ALLIES) (:303-320): g_merge_credit_of only holds players with a g_budget entry, so this guest is left out; then g_budget_name.clear() (:320) also drops a rejoiner's kept budget, and set_rivals(false) makes the late K_SET a no-op (:591).
* The guest's on_state (:490-495): a new merge seq with no credit for it sets g_merge_credit = 0, apply_pending_money (:423-431) sends d = *m - 0 = the whole money; the host adds it (K_MERGE_FIX, :601-605).
Correction of the audit's detail: for a first-time joiner the whole money is the money of the host's save it loaded, which BUG-0192 documents as that player's Rivals budget; the normal path (K_SET with have=0, then K_MONEY) would credit the same amount, so the first join alone is not a duplication. The money really goes wrong (inferred) in two cases: (a) a rejoiner the host has a budget for in g_budget_name: the merge adds the save's money instead of its kept budget, and the kept budget is cleared (gained or lost money = the difference); (b) a guest still loading when the switch comes: its merge waits in g_merge_pending until money_ptr() is valid, and if the host's shared total (shared.cpp on_message, MSG_RES) is written into its game in that first Space frame before space_rivalry's tick, d is the whole shared total and the host doubles it.

## Fix idea
The merge must not depend on what the guest has at the switch when the host never counted its budget: the host includes every roster player in the merge, with g_budget_name (rejoiner) or "unknown"; a guest without a credit in the K_STATE sends no K_MERGE_FIX for its whole money but its budget as the host would have known it (K_SET's answer first: hold the merge until K_SET came, or let the host send the credit it assumes). Also keep g_budget_name across the switch until every pending guest answered. Switch idea: `coop.fix space.rival_merge_unknown off`.

## How to test
Extend tools/tests/coop_space_rivalry.py (or a new coop_space_rival_merge_join.py): Rivals on, the guest leaves and rejoins (or joins fresh) and the host switches to Allies while the guest is still loading (and once in the same frame as its first K_ASK, a test hook that holds K_SET); assert the host's game money (the shared total in the game, not a counter) equals host budget + the guest's known budget. Main: FAIL (the total grows by the guest's save money or by the total itself).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
