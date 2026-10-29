---
id: BUG-0275
title: the extra guests (3rd and 4th player) stay over the sync limits after BUG-0269 (cell c 2.0, creature n 5.8)
status: open
kind: desync
stage: cell
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
After BUG-0269 (a fresh host sample per guest, distance from the host's path) lab 20261029-122937
campaign#1 still had "Carol: objects of kind c out of sync in cell (2.0)" (limit 1.2) and "Dave:
objects of kind n out of sync in creature (5.8)" (limit 3.0). Every desync finding of four campaign
runs was on the 3rd or 4th player (instances 20+N, 30+N), none on Bob. Open (?): whether those
instances run at a lower frame rate or get states later (4 games on one lane), or a real per-peer
problem of the host's streaming.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
