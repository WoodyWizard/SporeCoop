---
id: BUG-0959
title: Cell: the host's trace says a knocked-out guest's kill was applied before it refuses it
status: open
kind: bug
stage: Cell
reported: 2026-11-12
source: fix/w48-cell lab 2026-11-12 (coop_cell_kill_refused.py)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0821 BUG-0714
---

## Report
The host's trace line for a guest's kill (MSG_CELL_EAT) says "eat: from 1 kill c<key> (creature): applied" for a kill
it refuses in the next statement because the guest is knocked out ("bite: from 1 kill c<key> refused: that player is
knocked out", lab 20261112-065711 and 20261112-070304): a support log reads as if the creature had been killed.

## Root cause
Code: cellsync.cpp MSG_CELL_EAT handler, the trace::ev for a kill decides its word from `again` / `alive` before the
knockout check (`guest_knocked_out(from)`) that refuses it.

## Fix
Not fixed (diagnostics only). Say "refused (knocked out)" in the eat line when guest_knocked_out(from).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## How to test
`coop_cell_kill_refused.py`: the host log for the knocked-out guest's sendact must not say "applied".

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-cell): opened.
