---
id: BUG-9079
title: Guest trader reaches rival without creating a trade route
status: not-reproduced
kind: bug
stage: Civilization
reported: 2026-11-06
source: Native guest trade-control qualification
role: host guest
versions: duo21-dev
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests: tools/tests/duo_civ_trade_controls.py
signature: Guest native order 9 completes at rival with empty trade route vectors
docs: docs/re/civ.md
related: BUG-9077
---

## Report

On disposable TradeControlsC, an economic guest vehicle purchased through
the native city menu accepts a native create-route command (order 9).
The host targets the correct rival city, moves the vehicle there, and
mirrors its movement. On arrival the order ends; both endpoint cities'
native +55C trade-route vectors remain empty, and purchase progress stays
zero. No visible negotiation or native fault was reported on either peer.

## Evidence

build/local/lineages-trade-controls-c-retry.log proves native purchase.
build/local/lineages-trade-order-probe.log proves native guest selection
and correct host route target using that existing trader.
Read-only route inspection found zero entries in all three cities on both
peers after arrival. Repeated native buy-progress reads remained zero.

The earlier purchase-button disappearance was a test timing error:
input_click releases after six frames, but the test waited only 0.5 seconds
at 10 FPS. Waiting eight frames allowed the purchase. Broad drag selection
also overlapped another parked vehicle; a direct completed click selected
the trader. Neither is evidence of a production city-menu bug.

Root cause of the route refusal is not established. Check native diplomacy
eligibility and route-creation behavior before changing ownership hooks.

## Follow-up

Native relationship category reads returned 1 from guest to rival and 2
in the reverse direction. The verified route-acceptance helper CEEF90
returns zero when the requesting non-player nation's category is <= 1.
An ASLR-adjusted entry probe observed the actual route request calling it
with this rival city and guest nation. The branch for the later neutral
relationship case was not reached. This refusal is consistent with the
fixture's negative diplomacy, not evidence of broken command forwarding.
An eligible friendly trade fixture and guest trade-progress rendering still
need qualification. No gameplay patch is justified by this run.

The attempted allied fixture setup crashed because the diagnostic caller
used Space DeclareAlliance with political IDs; tracked separately as BUG-9080.

lineages-trade-controls-d.log, run after the religious and takeover
regressions, failed before input because the camera could not frame the
guest city. Both games still reported zero faults. It does not qualify
trade input or friendly acceptance; keep those checks separate from the
passed religious-bonus and takeover tests.

