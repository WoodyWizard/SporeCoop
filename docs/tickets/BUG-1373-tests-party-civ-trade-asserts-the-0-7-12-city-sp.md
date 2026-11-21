---
id: BUG-1373
title: Tests: party_civ_trade asserts the 0.7.12 city split: since BUG-0875 the host keeps the cities its captures took
status: open
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: 
fixed_in:
branch: fix/w61-civ
commits: 28b70c3d
tests: party_civ_trade
signature: FAIL [owners] three cities: Alice, Bob and Carol own one each, no shared capital ({0: 253, 1: 0, 2: 0})
docs: docs/re/civ.md#civ-capture-keeps
related: BUG-0875, BUG-0877, BUG-0152
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 4, cold): party_civ_trade `FAIL [owners] three cities:
Alice, Bob and Carol own one each, no shared capital ({0: 253, 1: 0, 2: 0})` and, from it, `FAIL [trade] three trading
pairs ... (+0, pairs 0)`. Its last runs before were 2026-11-01/02 (main, PASS: {0: 0, 1: 1, 2: 2}); it was not in a
lab run between 0.7.12 and this one.

## Root cause
Observed: the test lets the host capture both NPC cities (`coop.civ capture`, the captor is the nearest vehicle: the
host's) and expected them dealt to Bob and Carol, the round-robin of 0.7.12. BUG-0875 (0.7.13, civ.capture_keeps)
changed that on purpose: a captured city stays with its captor while that player is here, and the capital stays shared
while a player would have no city. So the host kept cities 1 and 2 (owner 0), the capital stayed shared (253), and no
two cities of different players were left to trade (pairs 0). The mod did what 0.7.13 decided; the test asserted the
old split. Not a mod bug.

## Fix
Test only (coop_party_civ_trade.py): Bob and Carol each buy one NPC city themselves (`coop.civ buycity <city> 500` on
the guest: the game's city purchase, which the host's game makes for that guest, BUG-0877), after the host's money was
raised by 30000; a specialty box on the host or the buyer is answered as before. The owners check is unchanged (Alice,
Bob and Carol one city each, no shared capital, the same on every machine), and so are the roles, trade and leave
steps.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (the old test against 0.7.13's rule): 20261120-133703:party_civ_trade.
- no-fail-run: a test correction (the test asserted the 0.7.12 split); BUG-0875's own twin civ_capture_keeps covers the
  rule. Open, waiting for the lab run: party_civ_trade must PASS.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause from the logs and the code; the guests buy their cities; no lab run (owner rule).
