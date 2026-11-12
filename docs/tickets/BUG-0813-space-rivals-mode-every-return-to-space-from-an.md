---
id: BUG-0813
title: Space rivals mode: every return to Space from an editor or adventure resets a guest's budget to the host's stale copy (money lost or created)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: c395848c
tests: space_rival_away
signature:
docs: docs/re/space.md#space-s1-0713
related: BUG-0192
---

## Report
In rivals mode a guest's own money jumps back after an editor or adventure visit: a contract reward paid outside Space disappears, or money spent just before the editor comes back. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_rivalry.cpp:665-667: `if (sp && !g_was_in_space) g_budget_known = false;`. money_ptr() is null also in editors and adventures started from Space, not only after a load. The guest then sends K_ASK; the host answers from g_budget_name, its last K_MONEY copy (:567-575); K_SET does `add_money(v - *m)` (:583-584). A BUG-0694 reward paid in the first Space frame, or a change in the last 0.5 s before the editor (the report throttle at :679), is undone. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Clear g_budget_known only when spaceext::world_epoch() changes (a real load), not on every return to Space.

## How to test
Host `coop.space rivalry mode rivals`; guest `coop.space contracts money` (M), `coop.space away 1`, `coop.space contracts result 5000 0`, `coop.space away 0`; after 3 s: FAIL = money M and `coop.space rivalry` shows restored=1; PASS = M+5000.

## Fix
A guest asks the host for its budget again (K_ASK) only when it comes into Space in another Space world
(spaceext::world_epoch, which an editor, the outfitter or an adventure entered from Space does not change), not on
every return to Space: the epoch at which the budget was known is kept (g_budget_epoch). Root cause confirmed at
runtime: with the switch off, a 5000 reward paid in the first Space frame after `away` was undone by the host's
K_SET (`restored` 0 -> 1, the money back to 50000). Switch `coop.fix space.rival_budget_world off` (0.7.12).
`coop.space rivalry` shows `budget_kept=` (returns that kept the budget).

## Verification
`coop_space_rival_away.py` (suite `space_rival_away`): rivals mode, the guest `away 1`, a 5000 contract result
while away, `away 0`; the guest's game money must be 50000 + 5000 and stay.
* FAIL with `SPORE_FIX_OFF=space.rival_budget_world`: lab 20261112-032943 (55000 for a moment, then 50000, restored=1).
* PASS: lab 20261112-032946 (55000 after 6 s, restored 0).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the budget is asked for again only in another Space world; switch space.rival_budget_world).
