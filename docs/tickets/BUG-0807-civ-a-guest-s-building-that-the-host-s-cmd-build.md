---
id: BUG-0807
title: Civ: a guest's building that the host's CMD_BUILD rejects costs the shared money and is then demolished on the guest, without a refund or a notice
status: open
kind: bug
stage: Civ
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
related:
---

## Report
A guest builds in a city's planner, the money is gone, and a moment later the building disappears from its city. Found by reading the code.

## Root cause
Observed in the code, confidence medium-high. The guest pays in its own planner (the money goes to the host through the shared ledger). The host's CMD_BUILD handler (civ.cpp:3053-3072) calls build_in_city(..., pay=false); when it fails (no free slot, building_model_ok false, the city's owner changed meanwhile, for example by the round-robin resplit) it only calls reject(): no refund, no MSG_NOTICE. reconcile_buildings (civ.cpp:3172-3175) then removes the guest's building as an extra one (DestroyBuilding). Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
On a rejected CMD_BUILD the host credits the building's cost (the building's price, vt 0x5C, as for a purchase) to the shared money and sends the guest a notice.

## How to test
Two instances in Civ; on the host make the build fail (fill the city's slots, or give the city to another player in the same 0.1 s); the guest buys a building: FAIL = the host log has `rejected command kind 3`, the shared money stays lower by the price and the guest's building is destroyed; PASS = the money comes back and the guest sees a notice.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
