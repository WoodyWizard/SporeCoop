---
id: BUG-0832
title: Space: a delivered spice sale pays the seller nothing when it is outside Space at the session end
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: d8dc662e
tests: space_sale_end
signature:
docs: docs/re/space.md#space-s1-0713
related: BUG-0196
---

## Report
A player sells spice to a partner, the spice arrives, and the money never comes because the seller was in an editor or adventure when the session ended. Found by reading the code; narrow window.

## Root cause
Observed in the code, confidence high. space_trade.cpp:1627 pays a delivered sale only `if (o.kind == K_SELL && here())`; there is no owed branch for a seller who is not here. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Owe the payment (as g_owed does for other trades) and pay it in the first Space frame.

## How to test
Receiver `coop.space trade hold 2`; seller `coop.space trade sell`, `coop.space trade away 1`, end the session: FAIL = `earned` unchanged after the seller is back in Space; PASS = paid.

## Fix
At the session end a delivered sale (BUG-0542: not given back) of a seller outside Space is owed (OwedSale: the
sum, the spice, the sale's world tag) and paid in the first Space frame of that game (spaceext::owed_action decides as
for refunds: Pay / Keep for another game / Drop). Root cause confirmed at runtime. Switch `coop.fix trade.sale_owed
off` (0.7.12). `coop.space trade`: `sales_owed=<waiting>/<owed>/<paid>/<dropped>`.

## Verification
`coop_space_sale_end.py` (suite `space_sale_end`): the guest sells 10 spice to the host's colony (the host `trade hold
2`: added, no answer), `trade away 1`, leaves, `trade away 0`.
* FAIL with `SPORE_FIX_OFF=trade.sale_owed`: lab 20261112-035215 (the guest's game money stays 100000).
* PASS: lab 20261112-034734 (100000 + 149940 once, `sales_owed=1/1/0/0` before the return).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the payment is owed and paid back in Space; switch trade.sale_owed).
