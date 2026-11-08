---
id: BUG-0695
title: Space: contract deadlines run on wall-clock time: a pause, a menu or a load uses them up
status: fixed
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: 1391b0ac
tests: space_contract_clock
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. space_contracts.cpp deadlines (offer deadline, active time limit) use H->time_seconds(), a wall clock: a paused game, the host in a menu or a long load runs a contract out with nobody able to play it.

## Root cause
Observed (lab): the deadlines ran on H->time_seconds: 12 s outside Space took 13 s off an offer (179.0 -> 165.9).

## Fix
`space.contract_clock`: the host's contract clock stands still while its game is paused (world::time_paused), outside Space or without frames (a gap over 1 s).

## Verification
* FAIL with `SPORE_FIX_OFF=space.contract_clock`: out/lab/20261107-225354.
* PASS: out/lab/20261107-224108, out/lab/20261107-225634.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
