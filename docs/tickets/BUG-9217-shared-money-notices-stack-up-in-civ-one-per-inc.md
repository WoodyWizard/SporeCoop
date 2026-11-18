---
id: BUG-9217
title: Shared money notices stack up in Civ (one per income tick)
status: fixed
kind: bug
stage: civ
reported: 2026-11-15
source: merge stage tour 2026-11-15
role: both
versions: merge-0.7.14
fixed_in: 0.7.14.1-lineages56-dev
branch: feat/lineages-merge-0.7.14
commits: 4a024f91
tests: manual Windows QA: the MSI, two games (host MSICivHost, guest MSIGuest), Abber.spo
signature:
docs:
related:
---

## Report
In the merge's stage tour (TITAN host, MSI guest, Civ, Abber.spo) the guest's Co-op chat filled with "Host earned ... money (shared)" lines, one per city payout every few seconds.

## Root cause
Observed: BUG-0696 (upstream 0.7.10) says every change of 25 or more by another player at once; a Civ host's city income arrives in many small payouts, each one a line.

## Fix
- One player's earnings (and, apart, spendings) of one resource are summed and said at most once per 10 s; a change after a quiet gap is said at the next tick (mods/coop/shared.cpp, switch shared.cause_merge).

## Verification
FAIL (MSI two games, Abber.spo Civ, switch off): 5 host spends of 100 at 1.5 s gave the guest 8 notices (spends and the refills to the cap).
PASS (switch on): 10 such changes gave 4 notices (each kind summed: one at once, one after the 10 s gap).
no-fail-run: Windows QA by hand (no lab on this PC); counted with the guest's coop.res causes=

## History
- 2026-11-15: opened.
- 2026-11-15: fixed for 56-dev, checked on the MSI (TITAN's test copy did not start after a Steam client update).
- 2026-11-15: released in 0.7.14.1-lineages56-dev.
