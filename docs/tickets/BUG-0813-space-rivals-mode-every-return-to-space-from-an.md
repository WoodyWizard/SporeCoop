---
id: BUG-0813
title: Space rivals mode: every return to Space from an editor or adventure resets a guest's budget to the host's stale copy (money lost or created)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
