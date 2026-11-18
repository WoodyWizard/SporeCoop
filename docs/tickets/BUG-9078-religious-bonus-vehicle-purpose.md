---
id: BUG-9078
title: Religious governor bonus looks for economic vehicles
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-06
source: Native trade-control qualification code review
role: host guest
versions: duo21-dev
fixed_in: unreleased
branch: feat/lineages-civ-combat
commits: f7283e26
tests: tools/tests/duo_civ_religious_bonus.py
signature: h_Convert requires religious role with vehicle purpose 2 instead of 1
docs: docs/re/civ.md
related: BUG-9077
---

## Report

h_Convert selects a nearby role-3 governor vehicle only when its purpose is
2. The native enum and existing RE notes identify purpose 1 as cultural
(religious) and 2 as economic. The proximity check also does not match the
vehicle faction to the converting faction, so an unrelated nearby governor
could boost another nation's conversion. The subsequent native regression
reproduced the trader bonus and qualifies the corrected faction attribution.

## Evidence

Spore ModAPI CommonIDs.h VehiclePurpose: military 0, cultural 1, economic 2.
https://github.com/Spore-Community/Spore-ModAPI/blob/master/Spore%20ModAPI/Spore/CommonIDs.h
The existing docs/re/civ.md vehicle and native order tables agree.

## Regression and fix

lineages-religious-bonus-a.log fails on the installed Duo 21 DLL: the
native guest-owned purpose-2 vehicle makes one citizen convert two.
Both games had zero native faults and quit normally. The fixture uses
native SpawnVehicle for the requested purpose with an available model,
then native Teleport to arrange proximity; ConvertCitizens supplies the
measured contribution, and the test reads the native follower map.

The civ.religious_attribution switch requires purpose 1 and the same
political ID as the converting faction. The off path keeps the old bug
for regression. Native role selection and vehicle-purpose mirrors are
checked across the two peers. Full mouse-driven preaching is separate.

## Verification

FAIL: lineages-religious-bonus-a.log on the installed Duo 21 DLL,
one citizen became two beside a trader.
PASS: lineages-religious-bonus-c.log and lineages-religious-bonus-d.log
on the patched source DLL: trader 1, own preacher 2, unrelated founder
undoes exactly 1 existing conversion. Both peers report zero faults.
lineages-religious-bonus-b.log caught a test assertion error about competing
faiths; it expected new followers instead of undoing existing conversion.

PASS: lineages-religious-takeover-d.log retains military and economic
115% effort and fractional religious progression. Windows unit tests end
ALL OK; documentation, symbol and glyph checks pass.
The later independent trade-input check in session D failed camera framing
(lineages-trade-controls-d.log); it is not counted as a trade pass.
Both session-D games then reported zero faults and quit normally.
