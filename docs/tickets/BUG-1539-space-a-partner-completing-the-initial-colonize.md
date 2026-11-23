---
id: BUG-1539
title: Space: a partner completing the initial colonize-planet tutorial mission blocks the other players from finishing it without destroying the partner's colony
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: discord
role: all
versions: 0.7.16
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/space.md
related: BUG-0181, BUG-0191
---

## Report
Player report in Discord (`#russian`, 2026-11-22, no report zip attached): in Space stage co-op, the initial tutorial mission asks to colonize a target planet; once one player places the colony, the remaining players can no longer complete their own tutorial mission unless they destroy their partner's colony and place a new one.

## Root cause
Not investigated yet. Space tutorial mission progression is tracked per game while colony placement is synced across players, so the second player's tutorial mission does not advance when the first player's colony satisfies the colonization objective.

## Fix
Not fixed. Target: **0.7.17.1** (not 0.7.17).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened for 0.7.17.1 from Discord `#russian` report (2026-11-22).
