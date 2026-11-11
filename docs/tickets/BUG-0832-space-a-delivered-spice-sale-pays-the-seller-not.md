---
id: BUG-0832
title: Space: a delivered spice sale pays the seller nothing when it is outside Space at the session end
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
