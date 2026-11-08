---
id: BUG-0696
title: Civ / Space: the shared money total (MSG_RES) carries no cause: a guest never learns whose earning or spending moved it
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: 1391b0ac
tests: res_cause
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. MSG_RES (money, food totals) and MSG_RES_ADD carry no cause, unlike the DNA total (BUG-0389): a guest's money jumps with no notice of whose purchase or earning it was.

## Root cause
Code: MSG_RES / MSG_RES_ADD carried no cause.

## Fix
`res.cause`: MSG_RES appends after its ack list u8 n, {u8 player, f64 change} since the host's last total (the host's own: its change minus the guests'); every game shows a notice for another player's change of 25 or more.

## Verification
* FAIL with `SPORE_FIX_OFF=res.cause`: out/lab/20261107-225209.
* PASS: out/lab/20261107-224702, out/lab/20261107-225634.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
