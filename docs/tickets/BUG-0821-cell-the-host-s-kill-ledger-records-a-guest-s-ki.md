---
id: BUG-0821
title: Cell: the host's kill ledger records a guest's kill before refusing it: that cell cannot be killed by guests for 10 s
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0632 BUG-0509
---

## Report
No player report. After a guest's kill the host refused (sent while knocked out, or another guest's kill of the same cell with 3-4 players), no guest can kill that creature for 10 s.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): cellsync.cpp:4146 `again = ... !g_kill_applied.first(idx, ...)` records the index before the knockout refusal at 4168 and before a guest_attack the game may spare; on the guest g_kill_sent.first (989 / 1025) also blocks resends for 10 s; every later lethal bite only counts a duplicate (h_Break). Confidence: medium (narrow window).

## Fix
Not fixed yet.

## Fix idea
Record in g_kill_applied only after the kill was applied (the victim dying); on the guest drop the g_kill_sent entry when the host's state still shows the cell alive after a round trip.

## How to test
Guest `coop.pool kodrop` / `sendact` to send a kill while knocked out, revive, bite the same cell to death: the host applies it (fix) vs 'refused (killed here already)' (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
