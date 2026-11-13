---
id: BUG-1029
title: Space: switching Rivals to Allies while a guest's budget is not known yet merges the wrong money (the save's money or the shared total instead of its budget)
status: fixed
kind: desync
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-1
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-money
commits: d500f3b0
tests: space_rival_merge
signature:
docs: docs/re/space.md#space-rival-merge-known, docs/known-issues.md#issue-rivals-merge-unknown-budget
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
space_rivalry.cpp, switch `coop.fix space.merge_known_budget` (off: 0.7.13):
* host_apply(A_ALLIES) credits every guest of the roster: its last K_MONEY (g_budget), else its kept budget by name
  (g_budget_name, a rejoiner), else 0 (a newcomer); the K_STATE carries a credit for each.
* apply_pending_money: a guest whose budget is not known in the current Space world (g_budget_known and
  g_budget_epoch == world_epoch; a rejoin or a load of the host's save makes it unknown) sends no K_MERGE_FIX: its
  game money is the save's (or the shared total), the host's credit is its budget. The split and K_SET now set
  g_budget_epoch at once.
* Owner decision 2 (2026-11-13): K_ASK of a name with no kept budget is answered `have=2, v=0`: a newcomer to a Rivals
  session starts its empire with 0, not with the host's save money; the host records that budget (a merge counts 0).
* Tests: `coop.space rivalry holdset <s>` (the host answers no K_ASK for s seconds); status `newcomers=`,
  `merge_fix_skipped=`.
Root cause confirmed at runtime (lab 20261113-005710, fix off): budgets host 49000 / guest 57000, the guest rejoins,
the host switches to Allies inside its load: both games 98000 = host 49000 + the save's 49000 (the rejoiner's 57000
lost; observed, not inferred). The "shared total doubled" variant (case b) was not seen: the guest's first K_STATE
after the rejoin already carried the merge, so no correction was pending; the fix covers it by the epoch rule
(inferred).

## Verification
`coop_space_rival_merge.py` (suite `space_rival_merge`, fixture space, ~2 min): Rivals, the guest earns 7000 and the
host spends 1000; the guest rejoins and the host switches to Allies while the guest's game loads: both games' Space
money (cEmpire +0xD0) must be host + the guest's kept budget. Then Rivals again and a new player (Carol) joins: its
game money must be 0, and the merge gives the host's budget only.
* FAIL (SPORE_FIX_OFF=space.merge_known_budget): out/lab/20261113-005710 (98000 instead of 106000; Carol 49000)
* PASS: out/lab/20261113-005935 (106000 on both; Carol 0, merge 53000 on both), out/lab/20261113-010140 (with space_rivalry and space_rival_away, all OK)

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-money (d500f3b0): the merge credits every roster guest, no correction with an unknown budget, newcomers start at 0; FAIL/PASS in the lab.
