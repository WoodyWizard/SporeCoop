---
id: BUG-0807
title: Civ: a guest's building that the host's CMD_BUILD rejects costs the shared money and is then demolished on the guest, without a refund or a notice
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-tribe-civ
commits: 2fc01638
tests: civ_build_refund
signature:
docs: docs/re/civ.md#civ-build-refund
related:
---

## Report
A guest builds in a city's planner, the money is gone, and a moment later the building disappears from its city. Found by reading the code.

## Root cause
Observed (code and lab). The guest pays in its own planner (the shared money); the host's CMD_BUILD handler called
build_in_city(..., pay=false) and, when it failed, only counted a rejected command (no refund, no notice);
reconcile_buildings then removed the guest's building as an extra one. Reproduced with the race the audit named: the
city's last free plot taken by the host's own building while the guest's build of that plot was on its way (a 300 ms
link): the host's game logged "no free slot", the money went down by two houses (3200) for one building (lab
20261112-085815).

## Fix idea
On a rejected CMD_BUILD the host credits the building's cost (the building's price, vt 0x5C, as for a purchase) to the shared money and sends the guest a notice.

## How to test
Two instances in Civ; on the host make the build fail (fill the city's slots, or give the city to another player in the same 0.1 s); the guest buys a building: FAIL = the host log has `rejected command kind 3`, the shared money stays lower by the price and the guest's building is destroyed; PASS = the money comes back and the guest sees a notice.

## Fix
civ.cpp `civ.build_refund`: the guest sends what it paid (a float after the place in CMD_BUILD: the planner path
sends the building's GetCost vt 0x5C, `coop.civ build` the money it spent; an older guest sends nothing). When the
host's game cannot build it (no plot, the city another player's or gone, the model missing, also for a build held
while the host's planner was open) the host gives that back to the shared money (cCivilization::AddMoney, at most
100000) and sends the guest a notice with the reason. Switch `coop.fix civ.build_refund off` (0.7.12).

## Verification
`coop_civ_build_refund.py` (suite `civ_build_refund`, civ fixture, SPORE_NET="delay=300", ~1.5 min): the host fills
its city until one plot is free (`coop.civ build`), then the guest and the host build a house in it in the same
moment; after 6 s one building more in each game and the money (each game's own) down by one house only.
* FAIL with `SPORE_FIX_OFF=civ.build_refund`: lab 20261112-085815 (spent 3200 for one house of 1600).
* PASS: lab 20261112-085612 (spent 1600, `build_refunds=1/1600`, "no free slot: 1600 back to the money"), 20261112-090844.

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12 (fix/w48-tribe-civ): reproduced (the last plot race), fixed (civ.build_refund), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
