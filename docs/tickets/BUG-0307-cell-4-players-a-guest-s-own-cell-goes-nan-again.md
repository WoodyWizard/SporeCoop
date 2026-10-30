---
id: BUG-0307
title: Cell, 4 players: a guest's own cell goes NaN again and again (26 times), its mirrors churn, fps falls to 1-3 and its address space drains
status: open
kind: desync
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign#2
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests: campaign, cell_4p
signature:
docs: docs/known-issues.md#issue-cell-own-nan
related: BUG-0299, BUG-0292, BUG-0306
---

## Report
Lab 20261030-104149 campaign#2 (main 813f8cd, lane 4), guest Carol (instance 24, the 3rd player, 900
mirrors, 160-250 m from the host): `our own cell's position is not finite (BUG-0292): 900 mirrors, 0
of them with a pose that is not finite` 26 times between 11:45:54 and 11:52:14 (game clock), healed
by BUG-0299's guard each time. Its mirror count swung 900 -> 307 -> 43 -> 900 ... between the 10 s
lines, the campaign's coverage for Carol fell to 57% and then 1% (9 mirrors), its fps went from 26
(avg) to 3, 2, 1, and its free address space from 3502 MB to 1427 MB in 6 minutes (the other three
games stayed at ~3500 MB). The first NaN came at fps ~26, before the slowdown.

## Root cause
Not known. Observed: no NaN mirror (BUG-0292's check), the guard heals the position, the NaN comes
back within seconds; the mirror churn follows (the host sends the cells around the guest's
reported position). Inferred: the address-space drain and the fps collapse follow the churn
(900 mirrors destroyed and created again per swing); not measured which allocation grows. The
host's fault loop BUG-0306 ran in the same minutes (from 11:47:35) and froze part of the host's
pool, which may have worsened the coverage. Source of the NaN: docs/known-issues.md#issue-cell-own-nan.

## Fix
Not fixed.

## Verification
Not reproduced on purpose yet.

## History
- 2026-10-30: opened.
